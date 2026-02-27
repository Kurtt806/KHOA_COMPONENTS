"""
Devices module - Quản lý danh sách thiết bị OTA (persist vào file JSON)
"""

import os
import json
from app.config import DEVICES_FILE
from app.utils import log_info, log_error


# State toàn cục
pending_devices: dict = {}      # {mac: {ip, chip, cores, ...status, timestamp}}
version_clients: dict = {}      # {ip: {count, last_time}}
active_downloads: dict = {}     # {ip: {percent, speed, downloaded, total}}
stats = {
    "download_count": 0,
    "version_check_count": 0,
}


def save_devices():
    """Lưu thiết bị vào JSON"""
    try:
        os.makedirs(os.path.dirname(DEVICES_FILE), exist_ok=True)
        with open(DEVICES_FILE, 'w', encoding='utf-8') as f:
            json.dump(pending_devices, f, ensure_ascii=False, indent=2)
    except Exception as e:
        log_error(f"Khong the luu devices: {e}")


def load_devices():
    """Đọc thiết bị từ JSON khi khởi động"""
    global pending_devices
    if os.path.isfile(DEVICES_FILE):
        try:
            with open(DEVICES_FILE, 'r', encoding='utf-8') as f:
                pending_devices = json.load(f)
            log_info(f"Da tai {len(pending_devices)} thiet bi tu {os.path.basename(DEVICES_FILE)}")
        except Exception as e:
            log_error(f"Khong the doc devices: {e}")
