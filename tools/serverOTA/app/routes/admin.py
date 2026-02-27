"""
Admin Routes - API cho Dashboard: duyệt thiết bị, upload firmware, set version
"""

import os
from datetime import datetime

from fastapi import APIRouter, Request, UploadFile, File, Form
from fastapi.responses import JSONResponse

from app.config import config
from app.devices import (
    pending_devices, version_clients, active_downloads, stats,
    save_devices, async_save_devices
)
from app.utils import (
    Colors, format_size, calc_md5,
    log_info, log_success, log_error,
)

router = APIRouter()


# Duyệt/từ chối thiết bị
@router.post("/approve-device")
async def handle_approve(request: Request):
    return await _handle_action(request, "approved")

@router.post("/deny-device")
async def handle_deny(request: Request):
    return await _handle_action(request, "denied")

async def _handle_action(request: Request, action: str):
    """Xử lý duyệt hoặc từ chối thiết bị"""
    try:
        data = await request.json()
    except Exception:
        return JSONResponse(content={"ok": False}, status_code=400)

    mac = data.get("mac", "")
    if not mac or mac not in pending_devices:
        return JSONResponse(content={"ok": False}, status_code=404)

    pending_devices[mac]["status"] = action
    if action == "approved":
        log_success(f"DUYET thiet bi MAC={mac}")
        print(f"{Colors.CYAN}{'─' * 50}{Colors.END}")
    else:
        log_error(f"TU CHOI thiet bi MAC={mac}")
        print(f"{Colors.CYAN}{'─' * 50}{Colors.END}")

    await async_save_devices()
    return JSONResponse(content={"ok": True, "status": action})


# Upload firmware .bin
@router.post("/api/upload-firmware")
async def upload_firmware(file: UploadFile = File(...), version: str = Form("")):
    """Upload firmware .bin từ Web UI"""
    if not file.filename or not file.filename.endswith('.bin'):
        return JSONResponse(content={"ok": False, "reason": "Chi nhan file .bin"}, status_code=400)

    fw_dir = config.firmware_dir or "/firmware"
    os.makedirs(fw_dir, exist_ok=True)
    dest = os.path.join(fw_dir, file.filename)

    try:
        with open(dest, "wb") as f:
            while True:
                chunk = await file.read(64 * 1024)
                if not chunk:
                    break
                f.write(chunk)
    except Exception as e:
        log_error(f"Loi luu firmware: {e}")
        return JSONResponse(content={"ok": False, "reason": str(e)}, status_code=500)

    config.firmware_path = os.path.abspath(dest)
    config.firmware_dir = os.path.abspath(fw_dir)
    if version.strip():
        config.ota_version = version.strip()

    fw_size = os.path.getsize(dest)
    fw_md5 = calc_md5(dest)
    log_success(f"Upload: {file.filename} ({format_size(fw_size)}) MD5: {fw_md5}")
    print(f"{Colors.CYAN}{'─' * 50}{Colors.END}")

    return JSONResponse(content={
        "ok": True,
        "filename": file.filename,
        "size": format_size(fw_size),
        "md5": fw_md5,
        "version": config.ota_version,
    })


# Set version
@router.post("/api/set-version")
async def set_version(request: Request):
    try:
        data = await request.json()
    except Exception:
        return JSONResponse(content={"ok": False}, status_code=400)

    new_ver = data.get("version", "").strip()
    if not new_ver:
        return JSONResponse(content={"ok": False, "reason": "Version rong"}, status_code=400)

    old_ver = config.ota_version
    config.ota_version = new_ver
    log_success(f"Version: {old_ver} → {new_ver}")
    return JSONResponse(content={"ok": True, "old_version": old_ver, "new_version": new_ver})


# API Data cho Dashboard
@router.get("/api/data")
async def serve_api_data():
    """Trả JSON cho Dashboard polling"""
    bin_files = []
    if config.firmware_path and os.path.isfile(config.firmware_path):
        size = os.path.getsize(config.firmware_path)
        mtime = datetime.fromtimestamp(os.path.getmtime(config.firmware_path)).strftime("%Y-%m-%d %H:%M:%S")
        bin_files.append({
            "name": os.path.basename(config.firmware_path),
            "size": format_size(size),
            "size_bytes": size,
            "time": mtime,
            "version": config.ota_version or "N/A",
            "md5": calc_md5(config.firmware_path),
        })

    return JSONResponse(content={
        "server": {
            "address": config.get_public_url(),
            "version": config.ota_version or "N/A",
            "version_checks": stats["version_check_count"],
            "downloads": stats["download_count"],
        },
        "version_clients": version_clients,
        "devices": pending_devices,
        "firmware_files": bin_files,
        "active_downloads": active_downloads,
    })
