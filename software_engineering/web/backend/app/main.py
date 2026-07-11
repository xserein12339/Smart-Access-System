"""
FastAPI 入口 — 挂路由、启动 MQTT hub、初始化 DB。
"""
import logging
import sys

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse

from .config import settings
from .db import init_db
from .mqtt_client import hub
from .routers import auth, devices, door, users, records, logs, firmware
from . import ws

# 强制配置 root + mqtt logger 输出到 stdout，覆盖 uvicorn 默认配置
_logging_fmt = logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s")
_handler = logging.StreamHandler(sys.stdout)
_handler.setFormatter(_logging_fmt)
_root = logging.getLogger()
_root.setLevel(logging.INFO)
_root.addHandler(_handler)
for _name in ("mqtt", "ws", "uvicorn", "uvicorn.access"):
    _lg = logging.getLogger(_name)
    _lg.setLevel(logging.INFO)
    if not _lg.handlers:
        _lg.addHandler(_handler)

app = FastAPI(title="Face Access IoT Backend", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(auth.router)
app.include_router(devices.router)
app.include_router(door.router)
app.include_router(users.router)
app.include_router(records.router)
app.include_router(logs.router)
app.include_router(firmware.router)
app.include_router(ws.router)

# 挂载静态文件目录，提供演示视频
import os
_static_dir = os.path.join(os.path.dirname(__file__), "..", "static")
if os.path.isdir(_static_dir):
    app.mount("/static", StaticFiles(directory=_static_dir), name="static")


@app.get("/demo", response_class=HTMLResponse)
async def demo_page():
    return """
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>演示视频</title>
<style>body{display:flex;justify-content:center;align-items:center;min-height:100vh;
margin:0;background:#000}video{max-width:100%;max-height:100vh}</style></head>
<body><video src="/static/demo.mp4" controls autoplay loop></video></body></html>"""


@app.on_event("startup")
async def _startup() -> None:
    print("==== startup: init_db + hub.start ====", flush=True)
    init_db()
    await hub.start()
    print(f"==== startup done, hub._running={hub._running} ====", flush=True)


@app.on_event("shutdown")
async def _shutdown() -> None:
    await hub.stop()


@app.get("/api/health")
def health():
    return {"status": "ok"}
