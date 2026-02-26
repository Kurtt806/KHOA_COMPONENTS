"""
Devices module - Quản lý danh sách thiết bị OTA (persist vào file JSON)
"""

import os
import json
from app.config import DEVICES_FILE
from app.utils import log_info, log_error


# ============================================================
# State toàn cục cho thiết bị
# ============================================================

# Thiết bị chờ duyệt/đã duyệt: {mac: {hash, ip, status, timestamp,...}}
pending_devices: dict = {}

# Thiết bị đã kiểm tra version: {ip: {count, last_time}}
version_clients: dict = {}

# Theo dõi tiến trình download realtime: {ip: {percent, speed, downloaded, total}}
active_downloads: dict = {}

# Thống kê request
stats = {
    "download_count": 0,
    "version_check_count": 0,
    "token_validate_count": 0,
}


# ============================================================
# Lưu/Đọc thiết bị từ file JSON (persist qua Docker volume)
# ============================================================

def save_devices():
    """Lưu danh sách thiết bị vào file JSON trên Docker volume"""
    try:
        os.makedirs(os.path.dirname(DEVICES_FILE), exist_ok=True)
        with open(DEVICES_FILE, 'w', encoding='utf-8') as f:
            json.dump(pending_devices, f, ensure_ascii=False, indent=2)
    except Exception as e:
        log_error(f"Khong the luu devices: {e}")


def load_devices():
    """Đọc danh sách thiết bị từ file JSON (khi server khởi động)"""
    global pending_devices
    if os.path.isfile(DEVICES_FILE):
        try:
            with open(DEVICES_FILE, 'r', encoding='utf-8') as f:
                pending_devices = json.load(f)
            log_info(f"Da tai {len(pending_devices)} thiet bi tu {os.path.basename(DEVICES_FILE)}")
        except Exception as e:
            log_error(f"Khong the doc devices file: {e}")
