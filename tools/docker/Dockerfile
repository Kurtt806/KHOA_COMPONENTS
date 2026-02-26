# ============================================================
# Dockerfile cho ESP32 OTA Server (FastAPI + Uvicorn)
# Tối ưu multi-stage build, image nhẹ ~50MB
# ============================================================

FROM python:3.12-slim AS base

# Metadata cho Portainer
LABEL maintainer="khoa"
LABEL description="ESP32 OTA Firmware Server with Web Dashboard"
LABEL version="2.0.0"

# Biến môi trường mặc định
ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    OTA_PORT=8080 \
    OTA_BIND=0.0.0.0 \
    OTA_FIRMWARE_DIR=/firmware \
    OTA_DATA_DIR=/data \
    OTA_VERSION=0.0.0 \
    OTA_TOKEN=""

WORKDIR /app

# Cài dependencies trước (tận dụng Docker cache)
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy source code
COPY app/ ./app/
COPY templates/ ./templates/
COPY static/ ./static/

# Tạo thư mục cho firmware và data (sẽ mount volume)
RUN mkdir -p /firmware /data

# Expose port mặc định
EXPOSE 8080

# Health check cho Portainer monitoring
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD python -c "import urllib.request; urllib.request.urlopen('http://localhost:${OTA_PORT}/api/data')" || exit 1

# Chạy server bằng uvicorn
CMD ["python", "-m", "uvicorn", "app.main:app", \
     "--host", "0.0.0.0", \
     "--port", "8080", \
     "--log-level", "warning", \
     "--proxy-headers", \
     "--forwarded-allow-ips", "*"]
