"""
Utils module - Các hàm tiện ích: log, format, hash, tìm firmware
"""

import os
import re
import socket
import hashlib
from pathlib import Path
from datetime import datetime


# ============================================================
# Màu sắc cho terminal
# ============================================================

class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BOLD = '\033[1m'
    END = '\033[0m'


# ============================================================
# Hàm log có timestamp và màu sắc
# ============================================================

def log_info(msg):
    """In thông báo với timestamp"""
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"{Colors.CYAN}[{ts}]{Colors.END} {msg}")

def log_success(msg):
    """In thông báo thành công"""
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"{Colors.GREEN}[{ts}] ✓ {msg}{Colors.END}")

def log_warning(msg):
    """In cảnh báo"""
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"{Colors.YELLOW}[{ts}] ⚠ {msg}{Colors.END}")

def log_error(msg):
    """In lỗi"""
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"{Colors.RED}[{ts}] ✗ {msg}{Colors.END}")


# ============================================================
# Hàm tiện ích chung
# ============================================================

def get_local_ip():
    """Lấy địa chỉ IP local của máy (hoặc container)"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def format_size(size_bytes):
    """Chuyển đổi bytes sang đơn vị dễ đọc (KB, MB)"""
    if size_bytes < 1024:
        return f"{size_bytes} B"
    elif size_bytes < 1024 * 1024:
        return f"{size_bytes / 1024:.1f} KB"
    else:
        return f"{size_bytes / (1024 * 1024):.2f} MB"


def calc_md5(filepath):
    """Tính MD5 hash của file firmware"""
    md5 = hashlib.md5()
    with open(filepath, 'rb') as f:
        for chunk in iter(lambda: f.read(8192), b''):
            md5.update(chunk)
    return md5.hexdigest()


def fnv1a_64(data: str) -> str:
    """Hash FNV-1a 64-bit - tương thích với thuật toán trên ESP32"""
    FNV_OFFSET = 14695981039346656037
    FNV_PRIME = 1099511628211
    MASK_64 = (1 << 64) - 1
    h = FNV_OFFSET
    for byte in data.encode('utf-8'):
        h ^= byte
        h = (h * FNV_PRIME) & MASK_64
    return f"{h:016x}"


def find_firmware(build_dir):
    """Tìm file firmware .bin trong thư mục (bỏ qua bootloader, partition)"""
    if not os.path.isdir(build_dir):
        return None
    bin_files = list(Path(build_dir).glob('*.bin'))
    app_bins = [f for f in bin_files if 'bootloader' not in f.name
                and 'partition' not in f.name
                and 'ota_data' not in f.name]
    if app_bins:
        return str(app_bins[0])
    return None


def read_project_version(search_dir):
    """Đọc PROJECT_VER từ CMakeLists.txt của project ESP-IDF"""
    for parent in [Path(search_dir).parent, Path(search_dir).parent.parent, Path('.')]:
        cmake_file = parent / 'CMakeLists.txt'
        if cmake_file.exists():
            content = cmake_file.read_text(encoding='utf-8', errors='ignore')
            match = re.search(r'set\s*\(\s*PROJECT_VER\s+"([^"]+)"\s*\)', content)
            if match:
                return match.group(1)
    return None
