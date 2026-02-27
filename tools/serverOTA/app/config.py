"""
Config module - Cấu hình server từ ENV (Docker) hoặc CLI args
"""

import os
import argparse
from typing import Optional
from app.utils import log_info, log_warning, find_firmware, read_project_version


class AppConfig:
    """Cấu hình server toàn cục"""
    firmware_path: Optional[str] = None
    firmware_dir: Optional[str] = None
    ota_version: str = "0.0.0"
    port: int = 8080
    bind: str = "0.0.0.0"
    base_url: Optional[str] = None  # URL public (VD: https://ota.vibohub.com)

    def get_public_url(self) -> str:
        """URL public cho ESP32 truy cập"""
        if self.base_url:
            return self.base_url.rstrip('/')
        from app.utils import get_local_ip
        return f"http://{get_local_ip()}:{self.port}"


config = AppConfig()

# Đường dẫn file dữ liệu
DATA_DIR = os.environ.get("OTA_DATA_DIR", "/data")
DEVICES_FILE = os.path.join(DATA_DIR, "ota_devices.json")

SCRIPT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATIC_DIR = os.path.join(SCRIPT_DIR, "static")
TEMPLATES_DIR = os.path.join(SCRIPT_DIR, "templates")


def configure_from_env_and_args():
    """Đọc config từ ENV hoặc CLI args"""
    env_firmware = os.environ.get("OTA_FIRMWARE")
    env_firmware_dir = os.environ.get("OTA_FIRMWARE_DIR", "/firmware")
    env_version = os.environ.get("OTA_VERSION")
    env_port = os.environ.get("OTA_PORT", "8080")
    env_bind = os.environ.get("OTA_BIND", "0.0.0.0")

    parser = argparse.ArgumentParser(description='OTA Server - Phuc vu firmware ESP32')
    parser.add_argument('--port', '-p', type=int, default=int(env_port))
    parser.add_argument('--firmware', '-f', type=str, default=env_firmware)
    parser.add_argument('--dir', '-d', type=str, default=env_firmware_dir)
    parser.add_argument('--bind', '-b', type=str, default=env_bind)
    parser.add_argument('--version', '-v', type=str, default=env_version)

    args = parser.parse_args()

    config.port = args.port
    config.bind = args.bind

    # Firmware path
    if args.firmware and os.path.isfile(args.firmware):
        config.firmware_path = os.path.abspath(args.firmware)
        config.firmware_dir = os.path.dirname(config.firmware_path)
    elif args.dir and os.path.isdir(args.dir):
        config.firmware_dir = os.path.abspath(args.dir)
        fw = find_firmware(args.dir)
        if fw:
            config.firmware_path = fw
    else:
        for search in ["/firmware", ".", SCRIPT_DIR]:
            if os.path.isdir(search):
                fw = find_firmware(search)
                if fw:
                    config.firmware_path = fw
                    config.firmware_dir = search
                    break

    # Version
    if args.version:
        config.ota_version = args.version
    else:
        search_dir = config.firmware_dir or '.'
        auto_version = read_project_version(search_dir)
        if auto_version:
            config.ota_version = auto_version
            log_info(f"Auto version tu CMakeLists.txt: {auto_version}")
        else:
            config.ota_version = "0.0.0"
            log_warning("Khong tim thay PROJECT_VER, dung 0.0.0")
