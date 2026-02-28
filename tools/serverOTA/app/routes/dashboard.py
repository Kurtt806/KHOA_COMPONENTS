"""
Dashboard Routes - Phục vụ giao diện web HTML/CSS/JS
"""
import os
from fastapi import APIRouter, HTTPException
from fastapi.responses import FileResponse
from app.config import TEMPLATES_DIR

router = APIRouter()

@router.get("/dashboard")
async def serve_dashboard():
    """Phục vụ trang Dashboard Web UI (index.html)"""
    html_path = os.path.join(TEMPLATES_DIR, 'index.html')
    if not os.path.isfile(html_path):
        raise HTTPException(status_code=404, detail="Dashboard UI not found")
    return FileResponse(html_path)
