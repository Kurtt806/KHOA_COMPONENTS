"""
Config module - Quản lý cấu hình server từ biến môi trường (Docker) hoặc arguments
"""

import os
import argparse
from typing import Optional
from app.utils import log_info, log_warning, find_firmware, read_project_version


class AppConfig:
    """Lưu cấu hình server toàn cục, đọc từ ENV hoặc CLI args"""
    firmware_path: Optional[str] = None
    firmware_dir: Optional[str] = None
    ota_version: str = "0.0.0"
    ota_token: Optional[str] = None
    port: int = 8080
    bind: str = "0.0.0.0"
    # URL public cho ESP32 truy cập (VD: http://ota.vibohub.com)
    # Nếu dùng domain + reverse proxy thì KHÔNG thêm port
    base_url: Optional[str] = None

    def get_public_url(self) -> str:
        """Trả về URL public để ESP32 truy cập firmware"""
        if self.base_url:
            return self.base_url.rstrip('/')
        from app.utils import get_local_ip
        return f"http://{get_local_ip()}:{self.port}"


# Singleton config
config = AppConfig()

# Đường dẫn file dữ liệu (Docker volume)
DATA_DIR = os.environ.get("OTA_DATA_DIR", "/data")
DEVICES_FILE = os.path.join(DATA_DIR, "ota_devices.json")

# Đường dẫn static/templates (relative to docker context)
SCRIPT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATIC_DIR = os.path.join(SCRIPT_DIR, "static")
TEMPLATES_DIR = os.path.join(SCRIPT_DIR, "templates")


def configure_from_env_and_args():
    """Đọc config từ biến môi trường (Docker) hoặc CLI arguments"""
    # Ưu tiên biến môi trường (Docker), fallback CLI args
    env_firmware = os.environ.get("OTA_FIRMWARE")
    env_firmware_dir = os.environ.get("OTA_FIRMWARE_DIR", "/firmware")
    env_version = os.environ.get("OTA_VERSION")
    env_token = os.environ.get("OTA_TOKEN")
    env_port = os.environ.get("OTA_PORT", "8080")
    env_bind = os.environ.get("OTA_BIND", "0.0.0.0")

    parser = argparse.ArgumentParser(
        description='OTA Server FastAPI - Phuc vu firmware cho ESP32 OTA (Docker-ready)',
    )
    parser.add_argument('--port', '-p', type=int, default=int(env_port))
    parser.add_argument('--firmware', '-f', type=str, default=env_firmware)
    parser.add_argument('--dir', '-d', type=str, default=env_firmware_dir)
    parser.add_argument('--bind', '-b', type=str, default=env_bind)
    parser.add_argument('--token', '-t', type=str, default=env_token)
    parser.add_argument('--version', '-v', type=str, default=env_version)

    args = parser.parse_args()

    config.port = args.port
    config.bind = args.bind
    config.ota_token = args.token

    # Xác định firmware path
    if args.firmware and os.path.isfile(args.firmware):
        config.firmware_path = os.path.abspath(args.firmware)
        config.firmware_dir = os.path.dirname(config.firmware_path)
    elif args.dir and os.path.isdir(args.dir):
        config.firmware_dir = os.path.abspath(args.dir)
        fw = find_firmware(args.dir)
        if fw:
            config.firmware_path = fw
    else:
        # Fallback: tìm trong /firmware (Docker mount) hoặc thư mục hiện tại
        for search in ["/firmware", ".", SCRIPT_DIR]:
            if os.path.isdir(search):
                fw = find_firmware(search)
                if fw:
                    config.firmware_path = fw
                    config.firmware_dir = search
                    break

    # Xác định version firmware
    if args.version:
        config.ota_version = args.version
    else:
        search_dir = config.firmware_dir or '.'
        auto_version = read_project_version(search_dir)
        if auto_version:
            config.ota_version = auto_version
            log_info(f"Tu dong doc version tu CMakeLists.txt: {auto_version}")
        else:
            config.ota_version = "0.0.0"
            log_warning("Khong tim thay PROJECT_VER, dung 0.0.0")
