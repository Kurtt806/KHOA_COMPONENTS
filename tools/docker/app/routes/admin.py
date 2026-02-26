"""
Admin Routes - API cho Web Dashboard: duyệt/từ chối thiết bị, upload firmware, set version
"""

import os
import shutil
from datetime import datetime

from fastapi import APIRouter, Request, UploadFile, File, Form
from fastapi.responses import JSONResponse

from app.config import config
from app.devices import (
    pending_devices, version_clients, active_downloads, stats,
    save_devices,
)
from app.utils import (
    get_local_ip, format_size, calc_md5,
    log_info, log_success, log_warning, log_error,
)

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
# Upload firmware .bin từ Web UI
# ============================================================

@router.post("/api/upload-firmware")
async def upload_firmware(
    file: UploadFile = File(...),
    version: str = Form(""),
):
    """Admin upload file firmware .bin và set version từ Web UI"""
    # Kiểm tra file .bin
    if not file.filename or not file.filename.endswith('.bin'):
        return JSONResponse(
            content={"ok": False, "reason": "Chi chap nhan file .bin"},
            status_code=400,
        )

    # Xác định thư mục lưu firmware
    fw_dir = config.firmware_dir or "/firmware"
    os.makedirs(fw_dir, exist_ok=True)

    # Lưu file firmware
    dest_path = os.path.join(fw_dir, file.filename)
    try:
        with open(dest_path, "wb") as f:
            # Đọc từng chunk để không chiếm hết RAM với file lớn
            while True:
                chunk = await file.read(64 * 1024)  # 64KB chunks
                if not chunk:
                    break
                f.write(chunk)
    except Exception as e:
        log_error(f"Loi khi luu firmware: {e}")
        return JSONResponse(
            content={"ok": False, "reason": f"Loi ghi file: {e}"},
            status_code=500,
        )

    # Cập nhật config
    config.firmware_path = os.path.abspath(dest_path)
    config.firmware_dir = os.path.abspath(fw_dir)

    # Cập nhật version nếu có
    if version.strip():
        config.ota_version = version.strip()

    fw_size = os.path.getsize(dest_path)
    fw_md5 = calc_md5(dest_path)

    log_success(f"📤 Upload firmware thanh cong: {file.filename}")
    log_info(f"   Kich thuoc: {format_size(fw_size)} | MD5: {fw_md5}")
    if version.strip():
        log_info(f"   Version: {version.strip()}")

    return JSONResponse(content={
        "ok": True,
        "filename": file.filename,
        "size": format_size(fw_size),
        "md5": fw_md5,
        "version": config.ota_version,
    })


# ============================================================
# Đổi version firmware từ Web UI
# ============================================================

@router.post("/api/set-version")
async def set_version(request: Request):
    """Admin đổi version firmware hiện tại từ Web UI"""
    try:
        data = await request.json()
    except Exception:
        return JSONResponse(content={"ok": False}, status_code=400)

    new_version = data.get("version", "").strip()
    if not new_version:
        return JSONResponse(
            content={"ok": False, "reason": "Version khong duoc de trong"},
            status_code=400,
        )

    old_version = config.ota_version
    config.ota_version = new_version
    log_success(f"📝 Doi version: {old_version} → {new_version}")

    return JSONResponse(content={
        "ok": True,
        "old_version": old_version,
        "new_version": new_version,
    })


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
