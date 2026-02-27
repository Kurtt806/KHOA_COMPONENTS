"""
Main module - Entry point OTA Server FastAPI
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


# Auto-configure từ ENV (Docker)
def _auto_configure_from_env():
    """Đọc config từ biến môi trường Docker"""
    from app.utils import find_firmware

    config.port = int(os.environ.get("OTA_PORT", "8080"))
    config.bind = os.environ.get("OTA_BIND", "0.0.0.0")
    config.ota_version = os.environ.get("OTA_VERSION", "0.0.0")
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


# FastAPI app
app = FastAPI(
    title="ESP32 OTA Server",
    description="Server OTA cho ESP32 - Bảo mật bằng MAC address",
    version="3.0.0",
    docs_url="/docs",
    redoc_url=None,
)

if os.path.isdir(STATIC_DIR):
    app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")

app.include_router(dashboard.router)
app.include_router(ota.router)
app.include_router(admin.router)


# Catch-all cho firmware .bin theo tên
@app.get("/{filename:path}")
async def catch_all_firmware(filename: str, request: Request):
    if filename.endswith('.bin'):
        return await ota.serve_firmware_by_name(filename, request)
    raise HTTPException(status_code=404, detail="Not found")


@app.on_event("startup")
async def startup_event():
    """In banner và load devices"""
    load_devices()
    local_ip = get_local_ip()

    print()
    print(f"{Colors.BOLD}{Colors.BLUE}╔══════════════════════════════════════════════╗{Colors.END}")
    print(f"{Colors.BOLD}{Colors.BLUE}║   ESP32 OTA Server v3.0 (MAC-based auth)    ║{Colors.END}")
    print(f"{Colors.BOLD}{Colors.BLUE}╚══════════════════════════════════════════════╝{Colors.END}")
    print()
    log_info(f"Server:  {Colors.BOLD}http://{local_ip}:{config.port}{Colors.END}")
    log_info(f"Version: {Colors.BOLD}{config.ota_version}{Colors.END}")

    if config.firmware_path and os.path.isfile(config.firmware_path):
        fw = config.firmware_path
        fw_size = os.path.getsize(fw)
        print()
        log_info(f"Firmware: {Colors.BOLD}{os.path.basename(fw)}{Colors.END} ({format_size(fw_size)})")
        log_info(f"MD5:      {calc_md5(fw)}")
    else:
        log_warning("Chua co firmware. Mount volume /firmware hoac upload qua Web UI.")

    print()
    print(f"{Colors.CYAN}{'─' * 50}{Colors.END}")
    log_info(f"📋 OTA Flow (2 buoc):")
    log_info(f"   B1: POST /            → ESP gui info, nhan version")
    log_info(f"   B2: GET /firmware.bin  → Download firmware")
    log_info(f"   🔑 Bao mat: MAC (Device-Id header)")
    print(f"{Colors.CYAN}{'─' * 50}{Colors.END}")
    print()
    log_info(f"Web UI:  {Colors.BOLD}http://{local_ip}:{config.port}/dashboard{Colors.END}")
    log_info(f"API:     {Colors.BOLD}http://{local_ip}:{config.port}/docs{Colors.END}")
    print()


if __name__ == "__main__":
    configure_from_env_and_args()
    uvicorn.run(app, host=config.bind, port=config.port, log_level="warning")
