"""设备状态路由 — GET /api/devices"""
from fastapi import APIRouter, Depends
from sqlmodel import Session, select

from ..auth import require_user
from ..db import DeviceStatus, get_session

router = APIRouter(prefix="/api", tags=["devices"])


@router.get("/devices")
def list_devices(s: Session = Depends(get_session), _u: str = Depends(require_user)):
    rows = s.exec(select(DeviceStatus)).all()
    return [
        {
            "device_id": r.device_id,
            "online": r.online,
            "ip": r.ip,
            "uptime_s": r.uptime_s,
            "fw": r.fw,
            "db_count": r.db_count,
            "last_seen": r.last_seen.isoformat() if r.last_seen else None,
        }
        for r in rows
    ]
