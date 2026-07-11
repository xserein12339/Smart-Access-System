"""通行记录路由 — GET /api/records 分页查询"""
from fastapi import APIRouter, Depends, Query
from sqlmodel import Session, select, desc

from ..auth import require_user
from ..db import RecordCache, get_session

router = APIRouter(prefix="/api/records", tags=["records"])

_RESULT_TXT = {0: "PASS", 1: "REJECT", 2: "ERROR"}
_METHOD_TXT = {0: "人脸", 1: "远程", 2: "其它"}


@router.get("")
def list_records(
    device_id: str | None = Query(None),
    limit: int = Query(50, ge=1, le=500),
    offset: int = Query(0, ge=0),
    s: Session = Depends(get_session),
    _u: str = Depends(require_user),
):
    stmt = select(RecordCache).order_by(desc(RecordCache.id))
    if device_id:
        stmt = stmt.where(RecordCache.device_id == device_id)
    rows = s.exec(stmt.offset(offset).limit(limit)).all()
    return {
        "items": [
            {
                "id": r.id,
                "device_id": r.device_id,
                "person_id": r.person_id,
                "timestamp": r.timestamp,
                "result": r.result,
                "result_txt": _RESULT_TXT.get(r.result, "?"),
                "method": r.method,
                "method_txt": _METHOD_TXT.get(r.method, "?"),
                "received_at": r.received_at.isoformat() if r.received_at else None,
            }
            for r in rows
        ],
        "limit": limit,
        "offset": offset,
    }
