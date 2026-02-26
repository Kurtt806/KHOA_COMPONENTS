# 🔧 ESP32 OTA Server - Docker Edition

Server HTTP phục vụ firmware OTA cho ESP32 với Web Dashboard realtime.  
Dùng **FastAPI** + **Uvicorn**, đóng gói **Docker**, quản lý qua **Portainer**.

## 📁 Cấu trúc thư mục

```
docker/
├── app/                    # Source code (modular)
│   ├── main.py             # Entry point, khởi tạo FastAPI
│   ├── config.py           # Cấu hình từ ENV/args
│   ├── utils.py            # Hàm tiện ích (log, hash, format)
│   ├── devices.py          # Quản lý thiết bị (persist JSON)
│   └── routes/
│       ├── ota.py          # API OTA: version, token, firmware
│       ├── admin.py        # API Admin: duyệt/từ chối, dashboard data
│       └── dashboard.py    # Serve HTML dashboard
├── templates/              # HTML template
├── static/                 # CSS + JS
├── firmware/               # ĐẶT FILE .BIN VÀO ĐÂY
├── Dockerfile
├── docker-compose.yml
└── requirements.txt
```

## 🚀 Cách deploy

### Cách 1: Portainer (Khuyến nghị)

1. Đẩy thư mục `docker/` lên server (scp, git, ...)
2. Mở **Portainer** → **Stacks** → **Add Stack**
3. Chọn **Upload** → upload file `docker-compose.yml`
4. Hoặc chọn **Web editor** → paste nội dung `docker-compose.yml`
5. Chỉnh biến môi trường nếu cần
6. Click **Deploy the stack**

### Cách 2: Docker CLI

```bash
# Build và chạy
cd tools/docker
docker-compose up -d --build

# Xem logs
docker-compose logs -f

# Dừng
docker-compose down
```

### Cách 3: Chạy trực tiếp (không Docker)

```bash
cd tools/docker
pip install -r requirements.txt
python -m app.main --port 8080 --version 1.0.0
```

## ⚙️ Biến môi trường

| Biến               | Mặc định           | Mô tả                            |
| ------------------ | ------------------ | -------------------------------- |
| `OTA_PORT`         | `8080`             | Port HTTP server                 |
| `OTA_VERSION`      | `0.0.0`            | Version firmware hiện tại        |
| `OTA_TOKEN`        | _(trống)_          | VIBO-KEY (trống = không yêu cầu) |
| `OTA_FIRMWARE_DIR` | `/firmware`        | Thư mục chứa file .bin           |
| `OTA_DATA_DIR`     | `/data`            | Thư mục lưu dữ liệu thiết bị     |
| `TZ`               | `Asia/Ho_Chi_Minh` | Timezone                         |

## 📦 Upload firmware

Đặt file `.bin` vào thư mục `firmware/`:

```bash
# Copy firmware vào thư mục mount
cp build/my_app.bin tools/docker/firmware/

# Hoặc dùng docker cp (container đang chạy)
docker cp my_app.bin esp32-ota-server:/firmware/
```

Sau đó restart container:

```bash
docker-compose restart
```

## 🔗 OTA Flow (3 bước)

```
ESP32                          Server
  │                              │
  ├── GET /version.json ──────→  │  B1: Kiểm tra version
  │  ← {version: "2.0.0"} ─────┤
  │                              │
  ├── POST /validate-token ────→ │  B2: Gửi token + MAC
  │  ← {status: "pending"} ────┤     Admin duyệt trên web
  │                              │
  ├── GET /token-status ───────→ │  B2b: Polling chờ duyệt
  │  ← {status: "approved"} ───┤
  │                              │
  ├── GET /firmware.bin ───────→ │  B3: Download firmware
  │  ← streaming binary ───────┤
  │                              │
  └── Reboot + report version    │  B5: Báo version mới
```

## 🌐 Web Endpoints

| Endpoint          | Method | Mô tả                     |
| ----------------- | ------ | ------------------------- |
| `/`               | GET    | Dashboard Web UI          |
| `/docs`           | GET    | Swagger API Documentation |
| `/api/data`       | GET    | JSON data cho dashboard   |
| `/version.json`   | GET    | Kiểm tra version firmware |
| `/validate-token` | POST   | Xác thực token ESP32      |
| `/token-status`   | GET    | Polling trạng thái duyệt  |
| `/firmware.bin`   | GET    | Download firmware         |
| `/approve-device` | POST   | Admin duyệt thiết bị      |
| `/deny-device`    | POST   | Admin từ chối thiết bị    |
