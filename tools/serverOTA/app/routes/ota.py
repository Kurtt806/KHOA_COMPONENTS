"""
OTA Routes - Xử lý luồng OTA 2 bước:
  B1: POST / → ESP gửi thông tin thiết bị, server trả version + firmware URL
  B2: GET /firmware.bin → Stream firmware download
Bảo mật: Dùng MAC (Device-Id header) làm định danh duy nhất
"""

import os
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
    Colors, format_size,
    log_info, log_success, log_warning, log_error,
)

router = APIRouter()


# ============================================================
# Bước 1: ESP32 POST thông tin thiết bị, server trả version
# ============================================================

@router.post("/")
async def handle_check_version(request: Request):
    """ESP32 POST {mac, version, chip, cores, flash_kb, app_name} → server trả firmware info"""
    stats["version_check_count"] += 1
    client_ip = request.client.host
    mac = request.headers.get("Device-Id", "")

    try:
        data = await request.json()
    except Exception:
        return JSONResponse(content={"error": "Invalid JSON"}, status_code=400)

    # Lấy thông tin từ body
    mac = mac or data.get("mac", "")
    device_version = data.get("version", "")

    if not mac:
        return JSONResponse(content={"error": "Missing mac/Device-Id"}, status_code=400)

    now = datetime.now().strftime("%H:%M:%S %d/%m/%Y")
    server_url = config.get_public_url()
    version = config.ota_version or "0.0.0"

    # Cập nhật thông tin thiết bị
    dev_info = {
        "ip": client_ip,
        "chip": data.get("chip", ""),
        "cores": data.get("cores", 0),
        "flash_kb": data.get("flash_kb", 0),
        "app_name": data.get("app_name", ""),
        "app_version": device_version,
        "timestamp": now,
        "status": pending_devices.get(mac, {}).get("status", "approved"),
    }
    pending_devices[mac] = dev_info
    save_devices()

    # Ghi nhận version check
    if client_ip in version_clients:
        version_clients[client_ip]["count"] += 1
        version_clients[client_ip]["last_time"] = now
    else:
        version_clients[client_ip] = {"count": 1, "last_time": now}

    log_info(f"🔍 [#{stats['version_check_count']}] Check tu {Colors.BOLD}{client_ip}{Colors.END} | MAC: {mac} | v{device_version}")

    # Tạo response JSON
    firmware_url = ""
    if config.firmware_path and os.path.isfile(config.firmware_path):
        firmware_url = f"{server_url}/{os.path.basename(config.firmware_path)}"

    response = {
        "firmware": {
            "version": version,
            "url": firmware_url,
            "force": 0,
        }
    }

    log_info(f"   Response: v{version} | URL: {firmware_url or '(none)'}")
    return JSONResponse(content=response)


# ============================================================
# Bước 2: ESP32 download firmware (streaming với progress)
# ============================================================

@router.get("/firmware.bin")
async def serve_firmware_default(request: Request):
    """Endpoint mặc định để download firmware"""
    if not config.firmware_path or not os.path.isfile(config.firmware_path):
        raise HTTPException(status_code=404, detail="Firmware not found")
    return await _stream_firmware(request, config.firmware_path)


async def _stream_firmware(request: Request, filepath: str):
    """Stream firmware file với progress log realtime"""
    stats["download_count"] += 1
    count = stats["download_count"]
    file_size = os.path.getsize(filepath)
    filename = os.path.basename(filepath)
    client_ip = request.client.host
    mac = request.headers.get("Device-Id", "?")

    print()
    log_info(f"{'=' * 50}")
    log_info(f"📥 OTA #{count} tu {Colors.BOLD}{client_ip}{Colors.END} | MAC: {mac}")
    log_info(f"   File: {filename} | Size: {format_size(file_size)}")
    log_info(f"{'=' * 50}")

    active_downloads[client_ip] = {
        "percent": 0, "speed": "0 B/s", "downloaded": 0, "total": file_size,
    }

    async def firmware_generator():
        """Stream firmware từng chunk 4KB"""
        sent = 0
        start_time = time.time()
        last_update_time = start_time

        try:
            with open(filepath, 'rb') as f:
                while True:
                    chunk = f.read(4096)
                    if not chunk:
                        break
                    yield chunk
                    sent += len(chunk)
                    percent = int((sent * 100) / file_size)

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
            log_error(f"Ket noi bi ngat: {client_ip}: {e}")
            print()
        finally:
            active_downloads.pop(client_ip, None)

    return StreamingResponse(
        firmware_generator(),
        media_type="application/octet-stream",
        headers={
            "Content-Length": str(file_size),
            "Content-Disposition": f'attachment; filename="{filename}"',
        },
    )


async def serve_firmware_by_name(filename: str, request: Request):
    """Phục vụ firmware theo tên file .bin"""
    if config.firmware_path and os.path.basename(config.firmware_path) == filename:
        return await _stream_firmware(request, config.firmware_path)

    if config.firmware_dir:
        filepath = os.path.join(config.firmware_dir, filename)
        if os.path.isfile(filepath):
            return await _stream_firmware(request, filepath)

    raise HTTPException(status_code=404, detail="Khong tim thay firmware")
