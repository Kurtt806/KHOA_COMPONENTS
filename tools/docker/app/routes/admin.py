"""
Admin Routes - API cho Web Dashboard: duyệt/từ chối thiết bị, lấy dữ liệu realtime
"""

import os
from datetime import datetime

from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse

from app.config import config
from app.devices import (
    pending_devices, version_clients, active_downloads, stats,
    save_devices,
)
from app.utils import get_local_ip, format_size, log_success, log_error

router = APIRouter()


# ============================================================
# Admin duyệt/từ chối thiết bị OTA
# ============================================================

@router.post("/approve-device")
async def handle_approve(request: Request):
    """Admin nhấn nút Cho phép trên web UI"""
    return await _handle_approve_deny(request, "approved")


@router.post("/deny-device")
async def handle_deny(request: Request):
    """Admin nhấn nút Từ chối trên web UI"""
    return await _handle_approve_deny(request, "denied")


async def _handle_approve_deny(request: Request, action: str):
    """Xử lý logic duyệt hoặc từ chối thiết bị"""
    try:
        data = await request.json()
    except Exception:
        return JSONResponse(content={"ok": False}, status_code=400)

    mac = data.get('mac', '')
    if not mac or mac not in pending_devices:
        return JSONResponse(content={"ok": False, "reason": "Not found"}, status_code=404)

    device = pending_devices[mac]
    device["status"] = action

    if action == "approved":
        log_success(f"✓ Admin DA DUYET thiet bi MAC={mac} (IP: {device['ip']})")
    else:
        log_error(f"✗ Admin TU CHOI thiet bi MAC={mac} (IP: {device['ip']})")

    save_devices()
    return JSONResponse(content={"ok": True, "status": action})


# ============================================================
# API Data cho Dashboard realtime (polling mỗi 1.5s)
# ============================================================

@router.get("/api/data")
async def serve_api_data():
    """Trả về toàn bộ dữ liệu JSON cho Dashboard Web UI realtime"""
    local_ip = get_local_ip()
    port = config.port

    bin_files = []
    if config.firmware_path and os.path.isfile(config.firmware_path):
        size = os.path.getsize(config.firmware_path)
        mtime = datetime.fromtimestamp(
            os.path.getmtime(config.firmware_path)
        ).strftime("%Y-%m-%d %H:%M:%S")
        rel_name = os.path.basename(config.firmware_path)
        bin_files.append({"name": rel_name, "size": format_size(size), "time": mtime})

    data = {
        "server": {
            "address": f"http://{local_ip}:{port}",
            "version": config.ota_version or "N/A",
            "version_checks": stats["version_check_count"],
            "downloads": stats["download_count"],
        },
        "version_clients": version_clients,
        "devices": pending_devices,
        "firmware_files": bin_files,
        "active_downloads": active_downloads,
    }
    return JSONResponse(content=data)
