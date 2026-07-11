"""
FastAPI 入口 — 挂路由、启动 MQTT hub、初始化 DB。
"""
import logging
import sys

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

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
