"""
OTA Routes - Xử lý luồng OTA 3 bước:
  B1: GET /version.json → Kiểm tra version
  B2: POST /validate-token → Xác thực token + MAC
  B3: GET /firmware.bin → Stream firmware download
"""

import os
import json
import time
from datetime import datetime

from fastapi import APIRouter, Request, HTTPException
from fastapi.responses import JSONResponse, StreamingResponse

from app.config import config
from app.devices import (
    pending_devices, version_clients, active_downloads, stats,
    save_devices,
)
from app.utils import (
    Colors, get_local_ip, format_size, fnv1a_64,
    log_info, log_success, log_warning, log_error,
)

router = APIRouter()


# ============================================================
# Bước 1: ESP32 kiểm tra version firmware mới
# ============================================================

@router.get("/version.json")
async def serve_version_info(request: Request, mac: str = "", v: str = ""):
    """ESP32 gọi endpoint này để kiểm tra có firmware mới không"""
    stats["version_check_count"] += 1
    client_ip = request.client.host
    version = config.ota_version or "0.0.0"
    response_data = {"version": version}

    # Cập nhật version hiện tại nếu thiết bị gửi kèm (sau khi OTA xong)
    if mac and v and mac in pending_devices:
        if pending_devices[mac].get('app_version') != v:
            pending_devices[mac]['app_version'] = v
            save_devices()
            log_success(f"🛈 B5: Cap nhat Version moi thiet bi [MAC: {mac}] -> v{v}")

    # Ghi nhận thiết bị kiểm tra version
    now = datetime.now().strftime("%H:%M:%S %d/%m/%Y")
    if client_ip in version_clients:
        version_clients[client_ip]["count"] += 1
        version_clients[client_ip]["last_time"] = now
    else:
        version_clients[client_ip] = {"count": 1, "last_time": now}

    log_info(f"🔍 [#{stats['version_check_count']}] Kiem tra version tu {Colors.BOLD}{client_ip}{Colors.END}")
    log_info(f"   Response: {json.dumps(response_data)}")
    return JSONResponse(content=response_data)


# ============================================================
# Bước 2: ESP32 gửi token hash + MAC để xác thực
# ============================================================

@router.post("/validate-token")
async def handle_validate_token(request: Request):
    """ESP gửi hash+MAC → server phân loại thiết bị và lưu chờ admin duyệt"""
    stats["token_validate_count"] += 1
    client_ip = request.client.host

    try:
        data = await request.json()
    except Exception:
        return JSONResponse(content={"status": "error", "reason": "Invalid JSON"}, status_code=400)

    received_hash = data.get('token_hash', '')
    received_mac = data.get('mac', '')

    if not received_mac:
        return JSONResponse(content={"status": "error", "reason": "Missing mac"}, status_code=400)

    now = datetime.now().strftime("%H:%M:%S %d/%m/%Y")

    # Lấy thông tin phần cứng từ ESP32
    dev_info = {
        "chip": data.get('chip', ''),
        "cores": data.get('cores', 0),
        "flash_kb": data.get('flash_kb', 0),
        "app_name": data.get('app_name', ''),
        "app_version": data.get('app_version', ''),
        "idf_version": data.get('idf_version', ''),
    }

    # Kiểm tra trạng thái duyệt trước đó (persist qua restart)
    current_status = "pending"
    if received_mac in pending_devices:
        if pending_devices[received_mac].get("status") == "approved":
            current_status = "approved"

    server_url = config.get_public_url()

    # Trường hợp 1: Không có hash → thiết bị chưa cấu hình VIBO-KEY
    if not received_hash:
        pending_devices[received_mac] = {
            "hash": "", "ip": client_ip, "status": current_status,
            "type": "no_key", "timestamp": now, **dev_info,
        }
        if current_status == "pending":
            log_warning(f"🔑 [#{stats['token_validate_count']}] Thiet bi CHUA CO KEY tu {Colors.BOLD}{client_ip}{Colors.END}")
            log_warning(f"   MAC: {received_mac} | {dev_info['chip']} | Flash: {dev_info['flash_kb']}KB")
            log_warning(f"   ⏳ CHO ADMIN KICH HOAT tren web: {server_url}/")
        else:
            log_success(f"🔑 [#{stats['token_validate_count']}] Thiet bi DA DUYET TU TRUOC (No Key) | MAC: {received_mac}")

        save_devices()
        response_data = {"status": current_status}
        if current_status == "approved" and config.firmware_path:
            response_data["firmware_url"] = f"{server_url}/{os.path.basename(config.firmware_path)}"
        return JSONResponse(content=response_data)

    # Trường hợp 2: Có hash → kiểm tra token hợp lệ
    token_valid = True
    expected_hash = ""
    if config.ota_token:
        expected_hash = fnv1a_64(config.ota_token + received_mac)
        token_valid = (received_hash == expected_hash)

    # Token sai → từ chối ngay
    if not token_valid:
        log_error(f"🔒 [#{stats['token_validate_count']}] Token KHONG HOP LE tu {Colors.BOLD}{client_ip}{Colors.END}")
        log_error(f"   MAC: {received_mac} | Hash: {received_hash} != {expected_hash}")
        return JSONResponse(content={"status": "denied", "reason": "Token mismatch"})

    # Token hợp lệ → cập nhật thông tin thiết bị
    pending_devices[received_mac] = {
        "hash": received_hash, "ip": client_ip, "status": current_status,
        "type": "verified", "timestamp": now, **dev_info,
    }

    if current_status == "pending":
        log_info(f"🔒 [#{stats['token_validate_count']}] Yeu cau OTA tu {Colors.BOLD}{client_ip}{Colors.END}")
        log_info(f"   MAC: {received_mac} | {dev_info['chip']} | Flash: {dev_info['flash_kb']}KB")
        log_info(f"   Token: ✓ hop le")
        log_warning(f"   ⏳ CHO ADMIN DUYET tren web: {server_url}/")
    else:
        log_success(f"🔒 [#{stats['token_validate_count']}] Thiet bi OTA hop le DA DUYET: {received_mac}")

    save_devices()
    response_data = {"status": current_status}
    if current_status == "approved" and config.firmware_path:
        response_data["firmware_url"] = f"{server_url}/{os.path.basename(config.firmware_path)}"
    return JSONResponse(content=response_data)


@router.get("/token-status")
async def handle_token_status(mac: str = ""):
    """ESP polling: GET /token-status?mac=xxx → trả trạng thái duyệt của admin"""
    if not mac:
        return JSONResponse(content={"status": "error"}, status_code=400)

    device = pending_devices.get(mac)
    if not device:
        return JSONResponse(content={"status": "unknown"})

    response_data = {"status": device["status"]}
    if device["status"] == "approved" and config.firmware_path:
        response_data["firmware_url"] = f"{config.get_public_url()}/{os.path.basename(config.firmware_path)}"
    return JSONResponse(content=response_data)


# ============================================================
# Bước 3: ESP32 download firmware (streaming với progress)
# ============================================================

@router.get("/firmware.bin")
async def serve_firmware_default(request: Request):
    """Endpoint mặc định để download firmware"""
    if not config.firmware_path or not os.path.isfile(config.firmware_path):
        raise HTTPException(status_code=404, detail="Firmware not found")
    return await _stream_firmware(request, config.firmware_path)


async def _stream_firmware(request: Request, filepath: str):
    """Stream firmware file với progress log realtime cho cả terminal và web UI"""
    stats["download_count"] += 1
    count = stats["download_count"]
    file_size = os.path.getsize(filepath)
    filename = os.path.basename(filepath)
    client_ip = request.client.host

    print()
    log_info(f"{'=' * 60}")
    log_info(f"📥 YEU CAU OTA #{count} tu {Colors.BOLD}{client_ip}{Colors.END}")
    log_info(f"   File: {filename} | Kich thuoc: {format_size(file_size)}")
    log_info(f"{'=' * 60}")

    active_downloads[client_ip] = {
        "percent": 0, "speed": "0 B/s", "downloaded": 0, "total": file_size,
    }

    async def firmware_generator():
        """Generator async - stream firmware từng chunk 4KB"""
        sent = 0
        chunk_size = 4096
        start_time = time.time()
        last_percent = -1
        last_update_time = start_time

        try:
            with open(filepath, 'rb') as f:
                while True:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    yield chunk
                    sent += len(chunk)
                    percent = int((sent * 100) / file_size)

                    # Update web UI tracking mỗi 0.25s
                    cur_time = time.time()
                    if cur_time - last_update_time > 0.25:
                        last_update_time = cur_time
                        speed = sent / (cur_time - start_time) if (cur_time - start_time) > 0 else 0
                        active_downloads[client_ip] = {
                            "percent": percent,
                            "speed": f"{format_size(int(speed))}/s",
                            "downloaded": sent,
                            "total": file_size,
                        }

            elapsed = time.time() - start_time
            speed = file_size / elapsed if elapsed > 0 else 0
            print()
            log_success(f"Hoan tat! {format_size(file_size)} trong {elapsed:.1f}s ({format_size(int(speed))}/s)")
            print()

        except Exception as e:
            print()
            log_error(f"Ket noi bi ngat boi client {client_ip}: {e}")
            print()
        finally:
            if client_ip in active_downloads:
                del active_downloads[client_ip]

    return StreamingResponse(
        firmware_generator(),
        media_type="application/octet-stream",
        headers={
            "Content-Length": str(file_size),
            "Content-Disposition": f'attachment; filename="{filename}"',
        },
    )


async def serve_firmware_by_name(filename: str, request: Request):
    """Phục vụ firmware theo tên file .bin (được gọi từ catch-all route)"""
    # Kiểm tra firmware_path mặc định
    if config.firmware_path and os.path.basename(config.firmware_path) == filename:
        return await _stream_firmware(request, config.firmware_path)

    # Tìm trong firmware_dir
    if config.firmware_dir:
        filepath = os.path.join(config.firmware_dir, filename)
        if os.path.isfile(filepath):
            return await _stream_firmware(request, filepath)

    raise HTTPException(status_code=404, detail="Khong tim thay file firmware")
