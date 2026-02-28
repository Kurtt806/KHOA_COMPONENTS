"""
OTA Routes - Xử lý luồng cập nhật firmware
"""
import os
import time
from datetime import datetime
from fastapi import APIRouter, Request, HTTPException
from fastapi.responses import JSONResponse, StreamingResponse

from app.config import config
from app.devices import (
    pending_devices, version_clients, active_downloads, stats,
    async_save_devices
)
from app.utils import Colors, format_size, log_esp_info, log_success, log_warning, log_error

router = APIRouter()

@router.post("/")
@router.post("/version.json")
@router.get("/version.json")
async def handle_check_version(request: Request):
    """Bước 1: Kiểm tra phiên bản"""
    stats["version_check_count"] += 1
    client_ip = request.client.host
    
    # Lấy info từ Headers hoặc Query hoặc Body
    mac = request.headers.get("Device-Id") or request.query_params.get("mac")
    version = request.query_params.get("v")
    
    body = {}
    if request.method == "POST":
        try: body = await request.json()
        except: pass
    
    mac = mac or body.get("mac", "")
    device_version = version or body.get("version", "") or request.headers.get("x-device-version", "")
    
    now = datetime.now().strftime("%H:%M:%S %d/%m/%Y")
    
    # Lưu info thiết bị
    if mac:
        pending_devices[mac] = {
            "ip": client_ip, "chip": body.get("chip", ""), "cores": body.get("cores", 0),
            "app_name": body.get("app_name", ""), "app_version": device_version,
            "timestamp": now, "status": pending_devices.get(mac, {}).get("status", "pending"),
        }
        await async_save_devices()

    # Track client
    client_stats = version_clients.get(client_ip, {"count": 0})
    client_stats["count"] += 1
    client_stats["last_time"] = now
    version_clients[client_ip] = client_stats

    # Logic duyệt & update
    fw_url = ""
    is_approved = (mac and pending_devices.get(mac, {}).get("status") == "approved")
    needs_update = (device_version != config.ota_version)

    if is_approved and needs_update and config.firmware_path:
        fw_url = f"{config.get_public_url()}/{os.path.basename(config.firmware_path)}"

    log_esp_info(f"🔍 [#{stats['version_check_count']}] {client_ip} ({mac}) v{device_version} -> v{config.ota_version} | {'OK' if fw_url else 'SKIP'}")

    return {
        "version": config.ota_version,
        "firmware": {"version": config.ota_version, "url": fw_url, "force": 0}
    }

@router.get("/firmware.bin")
async def serve_firmware_default(request: Request):
    if not config.firmware_path or not os.path.isfile(config.firmware_path):
        raise HTTPException(status_code=404, detail="Firmware file not found")
    return await _stream_firmware(request, config.firmware_path)

async def _stream_firmware(request: Request, filepath: str):
    """Stream firmware với progress log"""
    client_ip = request.client.host
    mac = request.headers.get("Device-Id")
    
    # Lookup MAC by IP if missing (compat)
    if not mac:
        mac = next((m for m, d in pending_devices.items() if d.get("ip") == client_ip), None)

    # Auth check
    if not mac or pending_devices.get(mac, {}).get("status") != "approved":
        log_warning(f"⛔ Unauthorized download: {client_ip} ({mac})")
        raise HTTPException(status_code=403, detail="Device not approved")

    stats["download_count"] += 1
    file_size = os.path.getsize(filepath)
    filename = os.path.basename(filepath)
    dl_key = mac
    
    log_esp_info(f"📥 OTA #{stats['download_count']} starting: {filename} ({format_size(file_size)}) to {client_ip}")

    async def gen():
        sent = 0
        last_pct = -1
        start_t = time.time()
        try:
            with open(filepath, 'rb') as f:
                while chunk := f.read(8192): # Tăng chunk size lên 8KB
                    yield chunk
                    sent += len(chunk)
                    pct = int(sent * 100 / file_size)
                    if pct != last_pct:
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] {Colors.BLUE}[OTA] Tai: {pct}% ({sent}/{file_size}){Colors.END}", flush=True)
                        last_pct = pct
                    
                    # Cập nhật global stats cho dashboard
                    active_downloads[dl_key] = {"percent": pct, "downloaded": sent, "total": file_size, "ip": client_ip}
            
            dur = time.time() - start_t
            log_success(f"✓ Download complete in {dur:.1f}s")
        except Exception as e:
            log_error(f"✗ Download failed: {e}")
        finally:
            active_downloads.pop(dl_key, None)

    return StreamingResponse(gen(), media_type="application/octet-stream", headers={
        "Content-Length": str(file_size),
        "Content-Disposition": f'attachment; filename="{filename}"'
    })

async def serve_firmware_by_name(filename: str, request: Request):
    target = None
    if config.firmware_path and os.path.basename(config.firmware_path) == filename:
        target = config.firmware_path
    elif config.firmware_dir:
        path = os.path.join(config.firmware_dir, filename)
        if os.path.isfile(path): target = path
    
    if target: return await _stream_firmware(request, target)
    raise HTTPException(status_code=404, detail="Firmware not found")
