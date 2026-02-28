"""
Config module - Quản lý cấu hình server OTA
"""

import os
import argparse
from typing import Optional
from pathlib import Path
from app.utils import log_info, log_warning, find_firmware, read_project_version

class AppConfig:
    def __init__(self):
        self.port: int = int(os.environ.get("OTA_PORT", "8080"))
        self.bind: str = os.environ.get("OTA_BIND", "0.0.0.0")
        self.ota_version: str = os.environ.get("OTA_VERSION", "0.0.0")
        self.base_url: Optional[str] = os.environ.get("OTA_BASE_URL")
        
        self.firmware_path: Optional[str] = os.environ.get("OTA_FIRMWARE")
        self.firmware_dir: str = os.environ.get("OTA_FIRMWARE_DIR", "/firmware")
        
        # Đường dẫn dữ liệu
        self.data_dir: str = os.environ.get("OTA_DATA_DIR", "/data")
        self.devices_file: str = os.path.join(self.data_dir, "ota_devices.json")
        
        # Đường dẫn code
        self.script_dir: str = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.static_dir: str = os.path.join(self.script_dir, "static")
        self.templates_dir: str = os.path.join(self.script_dir, "templates")

    def auto_detect(self):
        """Tự động tìm firmware và version từ môi trường xung quanh"""
        # 1. Tìm Firmware
        if self.firmware_path and os.path.isfile(self.firmware_path):
            self.firmware_path = os.path.abspath(self.firmware_path)
            self.firmware_dir = os.path.dirname(self.firmware_path)
        else:
            # Tìm trong các folder khả nghi
            for search in [self.firmware_dir, ".", self.script_dir]:
                if os.path.isdir(search):
                    fw = find_firmware(search)
                    if fw:
                        self.firmware_path = fw
                        self.firmware_dir = os.path.abspath(search)
                        break

        # 2. Tìm Version từ CMakeLists.txt nếu chưa có
        if self.ota_version == "0.0.0":
            search_dir = self.firmware_dir or '.'
            auto_ver = read_project_version(search_dir)
            if auto_ver:
                self.ota_version = auto_ver
                log_info(f"Auto-detected version: {auto_ver}")

    def load_args(self):
        """Ghi đè cấu hình từ CLI args"""
        parser = argparse.ArgumentParser(description='OTA Server for ESP32')
        parser.add_argument('--port', '-p', type=int, help='Server port')
        parser.add_argument('--bind', '-b', type=str, help='Bind address')
        parser.add_argument('--firmware', '-f', type=str, help='Path to firmware .bin')
        parser.add_argument('--version', '-v', type=str, help='Firmware version')
        
        args, _ = parser.parse_known_args()
        if args.port: self.port = args.port
        if args.bind: self.bind = args.bind
        if args.firmware: self.firmware_path = os.path.abspath(args.firmware)
        if args.version: self.ota_version = args.version

    def get_public_url(self) -> str:
        if self.base_url:
            return self.base_url.rstrip('/')
        from app.utils import get_local_ip
        return f"http://{get_local_ip()}:{self.port}"

# Singleton instance
config = AppConfig()
DATA_DIR = config.data_dir
DEVICES_FILE = config.devices_file
STATIC_DIR = config.static_dir
TEMPLATES_DIR = config.templates_dir
