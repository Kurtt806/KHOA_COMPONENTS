"""
Main module - Entry point cho OTA Server FastAPI
Khởi tạo app, mount routes, in banner khi khởi động
"""

import os

from fastapi import FastAPI, Request, HTTPException
from fastapi.staticfiles import StaticFiles

from app.config import config, configure_from_env_and_args, STATIC_DIR
from app.devices import load_devices
from app.utils import (
    Colors, get_local_ip, format_size, calc_md5,
    log_info, log_warning,
)
from app.routes import ota, admin, dashboard

import uvicorn


# ============================================================
# Auto-configure từ ENV khi chạy qua Docker CMD (uvicorn app.main:app)
# Khi chạy trực tiếp (python -m app.main), configure_from_env_and_args() sẽ gọi lại
# ============================================================

def _auto_configure_from_env():
    """Đọc config trực tiếp từ biến môi trường Docker (không cần CLI args)"""
    import os
    from app.utils import find_firmware, log_info, log_warning

    config.port = int(os.environ.get("OTA_PORT", "8080"))
    config.bind = os.environ.get("OTA_BIND", "0.0.0.0")
    config.ota_token = os.environ.get("OTA_TOKEN", "") or None
    config.ota_version = os.environ.get("OTA_VERSION", "0.0.0")
    # URL public cho ESP32 (VD: http://ota.vibohub.com - không có port)
    config.base_url = os.environ.get("OTA_BASE_URL", "") or None

    fw_env = os.environ.get("OTA_FIRMWARE")
    fw_dir = os.environ.get("OTA_FIRMWARE_DIR", "/firmware")

    if fw_env and os.path.isfile(fw_env):
        config.firmware_path = os.path.abspath(fw_env)
        config.firmware_dir = os.path.dirname(config.firmware_path)
    elif os.path.isdir(fw_dir):
        config.firmware_dir = os.path.abspath(fw_dir)
        fw = find_firmware(fw_dir)
        if fw:
            config.firmware_path = fw

_auto_configure_from_env()


# ============================================================
# Khởi tạo FastAPI app
# ============================================================

app = FastAPI(
    title="ESP32 OTA Server",
    description="Server HTTP phục vụ firmware OTA cho ESP32 với Web Dashboard",
    version="2.0.0",
    docs_url="/docs",
    redoc_url=None,
)

# Mount thư mục static (CSS, JS)
if os.path.isdir(STATIC_DIR):
    app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")

# Đăng ký các routes
app.include_router(dashboard.router)
app.include_router(ota.router)
app.include_router(admin.router)


# ============================================================
# Catch-all route cho firmware .bin theo tên
# ============================================================

@app.get("/{filename:path}")
async def catch_all_firmware(filename: str, request: Request):
    """Route bắt tất cả request .bin còn lại → phục vụ firmware theo tên"""
    if filename.endswith('.bin'):
        return await ota.serve_firmware_by_name(filename, request)
    raise HTTPException(status_code=404, detail="Khong tim thay")


# ============================================================
# Startup event - In banner và load dữ liệu
# ============================================================

@app.on_event("startup")
async def startup_event():
    """In banner thông tin server và load danh sách thiết bị đã lưu"""
    load_devices()
    local_ip = get_local_ip()

    print()
    print(f"{Colors.BOLD}{Colors.BLUE}╔══════════════════════════════════════════════════════════╗{Colors.END}")
    print(f"{Colors.BOLD}{Colors.BLUE}║   🔧  ESP32 OTA Server (FastAPI + Docker)  🔧          ║{Colors.END}")
    print(f"{Colors.BOLD}{Colors.BLUE}╚══════════════════════════════════════════════════════════╝{Colors.END}")
    print()
    log_info(f"Server:     {Colors.BOLD}http://{local_ip}:{config.port}{Colors.END}")
    log_info(f"Bind:       {config.bind}:{config.port}")
    log_info(f"Version:    {Colors.BOLD}{config.ota_version}{Colors.END}")

    if config.ota_token:
        log_info(f"Token:      {Colors.BOLD}{config.ota_token}{Colors.END} (Bat buoc)")
    else:
        log_info(f"Token:      {Colors.YELLOW}Khong yeu cau{Colors.END}")

    if config.firmware_path and os.path.isfile(config.firmware_path):
        fw = config.firmware_path
        fw_size = os.path.getsize(fw)
        fw_md5 = calc_md5(fw)
        print()
        log_info(f"Firmware:   {Colors.BOLD}{os.path.basename(fw)}{Colors.END}")
        log_info(f"Kich thuoc: {format_size(fw_size)}")
        log_info(f"MD5:        {fw_md5}")
        log_info(f"Duong dan:  {fw}")
    else:
        log_warning("Khong tim thay firmware cu the. Hay mount volume /firmware.")

    if config.firmware_dir:
        log_info(f"Thu muc:    {config.firmware_dir}")

    print()
    print(f"{Colors.CYAN}{'─' * 60}{Colors.END}")
    log_info(f"📋 OTA Flow:")
    log_info(f"   B1: GET /version.json  → Kiem tra version")
    log_info(f"   B2: POST /validate-token → Xac thuc token")
    log_info(f"   B3: GET /firmware.bin  → Download firmware")
    print(f"{Colors.CYAN}{'─' * 60}{Colors.END}")
    print()
    log_info(f"Web UI:     {Colors.BOLD}http://{local_ip}:{config.port}/{Colors.END}")
    log_info(f"API Docs:   {Colors.BOLD}http://{local_ip}:{config.port}/docs{Colors.END}")
    print()


# ============================================================
# Entry point
# ============================================================

if __name__ == "__main__":
    configure_from_env_and_args()
    uvicorn.run(
        app,
        host=config.bind,
        port=config.port,
        log_level="warning",
    )
