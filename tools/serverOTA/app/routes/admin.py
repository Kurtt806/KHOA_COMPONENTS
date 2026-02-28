"""
Admin Routes - API cho Dashboard
"""
import os
import aiofiles
from datetime import datetime
from fastapi import APIRouter, Request, UploadFile, File, Form, HTTPException

from app.config import config
from app.devices import pending_devices, version_clients, active_downloads, stats, async_save_devices
from app.utils import format_size, calc_md5, log_success, log_error, extract_version_from_filename

router = APIRouter(prefix="/api")

@router.post("/approve-device")
async def handle_approve(request: Request):
    return await _handle_action(request, "approved")

@router.post("/deny-device")
async def handle_deny(request: Request):
    return await _handle_action(request, "denied")

async def _handle_action(request: Request, action: str):
    data = await request.json()
    mac = data.get("mac", "")
    if not mac or mac not in pending_devices:
        raise HTTPException(status_code=404, detail="Device not found")

    pending_devices[mac]["status"] = action
    log_success(f"{action.upper()} MAC: {mac}")
    await async_save_devices()
    return {"ok": True, "status": action}

@router.post("/upload-firmware")
async def upload_firmware(file: UploadFile = File(...), version: str = Form("")):
    if not file.filename.endswith('.bin'):
        raise HTTPException(status_code=400, detail="Only .bin files allowed")

    fw_dir = config.firmware_dir or "/firmware"
    os.makedirs(fw_dir, exist_ok=True)
    dest = os.path.join(fw_dir, file.filename)

    try:
        async with aiofiles.open(dest, "wb") as f:
            while chunk := await file.read(64 * 1024):
                await f.write(chunk)
    except Exception as e:
        log_error(f"Save error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

    config.firmware_path = os.path.abspath(dest)
    
    # 1. Ưu tiên version từ form
    # 2. Nếu trống, tự động lấy từ tên file (vd: KHOA_COMPONENTS_v1.0.3.bin)
    new_ver = version.strip() or extract_version_from_filename(file.filename)
    if new_ver:
        config.ota_version = new_ver

    fw_size = os.path.getsize(dest)
    log_success(f"Uploaded: {file.filename} ({format_size(fw_size)}) v{config.ota_version}")
    return {"ok": True, "version": config.ota_version, "size": format_size(fw_size)}

@router.post("/set-version")
async def set_version(request: Request):
    data = await request.json()
    new_ver = data.get("version", "").strip()
    if not new_ver: raise HTTPException(status_code=400, detail="Empty version")

    old_ver = config.ota_version
    config.ota_version = new_ver
    log_success(f"Version updated: {old_ver} -> {new_ver}")
    return {"ok": True, "old": old_ver, "new": new_ver}

@router.get("/data")
async def serve_api_data():
    fw_info = None
    if config.firmware_path and os.path.isfile(config.firmware_path):
        st = os.stat(config.firmware_path)
        fw_info = {
            "name": os.path.basename(config.firmware_path),
            "size": format_size(st.st_size),
            "time": datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d %H:%M:%S"),
            "md5": calc_md5(config.firmware_path)
        }

    return {
        "server": {
            "address": config.get_public_url(),
            "version": config.ota_version,
            "checks": stats["version_check_count"],
            "downloads": stats["download_count"],
        },
        "version_clients": version_clients,
        "devices": pending_devices,
        "firmware": fw_info,
        "active_downloads": active_downloads,
    }
