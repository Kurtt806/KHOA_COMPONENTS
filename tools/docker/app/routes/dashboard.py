"""
Dashboard Routes - Phục vụ giao diện web HTML/CSS/JS
"""

import os

from fastapi import APIRouter, HTTPException
from fastapi.responses import HTMLResponse

from app.config import TEMPLATES_DIR

router = APIRouter()


@router.get("/dashboard", response_class=HTMLResponse)
async def serve_dashboard():
    """Phục vụ trang Dashboard Web UI (index.html)"""
    html_path = os.path.join(TEMPLATES_DIR, 'index.html')
    if not os.path.isfile(html_path):
        raise HTTPException(status_code=404, detail="Dashboard HTML not found")
    with open(html_path, 'r', encoding='utf-8') as f:
        return HTMLResponse(content=f.read())
