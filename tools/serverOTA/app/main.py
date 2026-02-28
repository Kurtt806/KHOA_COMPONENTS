"""
OTA Server - ESP32 Firmware Management System
"""
import os
import uvicorn
from fastapi import FastAPI, Request, HTTPException
from fastapi.staticfiles import StaticFiles
from contextlib import asynccontextmanager

from app.config import config, STATIC_DIR
from app.devices import load_devices
from app.utils import Colors, get_local_ip, format_size, calc_md5, log_info, log_warning
from app.routes import ota, admin, dashboard

@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup logic
    load_devices()
    config.auto_detect()
    
    local_ip = get_local_ip()
    print(f"\n{Colors.CYAN}🚀 ESP32 OTA Server starting...{Colors.END}")
    print(f"{Colors.BLUE}╔{'═'*50}╗")
    print(f"║ URL:     http://{local_ip}:{config.port:<30} ║")
    print(f"║ Version: {config.ota_version:<38} ║")
    
    if config.firmware_path and os.path.isfile(config.firmware_path):
        fw_name = os.path.basename(config.firmware_path)
        fw_size = format_size(os.path.getsize(config.firmware_path))
        print(f"║ File:    {fw_name:<30} ({fw_size:>7}) ║")
        print(f"║ MD5:     {calc_md5(config.firmware_path):<38} ║")
    else:
        log_warning("No firmware found! Please upload via Web UI.")
        
    print(f"╚{'═'*50}╝{Colors.END}")
    log_info(f"Dashboard: http://{local_ip}:{config.port}/dashboard")
    yield
    # Shutdown logic (nếu cần)

app = FastAPI(
    title="OTA Server",
    version="3.1.0",
    lifespan=lifespan
)

# Static files
if os.path.isdir(STATIC_DIR):
    app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")

# Routes
app.include_router(dashboard.router)
app.include_router(ota.router)
app.include_router(admin.router)

@app.get("/{filename:path}")
async def catch_all_firmware(filename: str, request: Request):
    if filename.endswith('.bin'):
        return await ota.serve_firmware_by_name(filename, request)
    raise HTTPException(status_code=404, detail="File not found")

if __name__ == "__main__":
    config.load_args()
    uvicorn.run(app, host=config.bind, port=config.port, log_level="warning")
