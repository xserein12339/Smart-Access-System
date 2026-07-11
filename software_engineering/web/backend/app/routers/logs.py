"""远程日志路由 — GET /api/logs 分页查询"""
from fastapi import APIRouter, Depends, Query
from sqlmodel import Session, select, desc

from ..auth import require_user
from ..db import LogCache, get_session

router = APIRouter(prefix="/api/logs", tags=["logs"])

_LV_TXT = {0: "ERROR", 1: "WARN", 2: "INFO", 3: "DEBUG"}


@router.get("")
def list_logs(
    device_id: str | None = Query(None),
    limit: int = Query(100, ge=1, le=1000),
    offset: int = Query(0, ge=0),
    s: Session = Depends(get_session),
    _u: str = Depends(require_user),
):
    stmt = select(LogCache).order_by(desc(LogCache.id))
    if device_id:
        stmt = stmt.where(LogCache.device_id == device_id)
    rows = s.exec(stmt.offset(offset).limit(limit)).all()
    return {
        "items": [
            {
                "id": r.id,
                "device_id": r.device_id,
                "lv": r.lv,
                "lv_txt": _LV_TXT.get(r.lv, "?"),
                "module": r.module,
                "ts": r.ts,
                "msg": r.msg,
                "received_at": r.received_at.isoformat() if r.received_at else None,
            }
            for r in rows
        ],
        "limit": limit,
        "offset": offset,
    }
