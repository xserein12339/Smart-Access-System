"""开门路由 — POST /api/door/open 下发 remote_open 并等 cmd_ack"""
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from ..auth import require_user
from ..mqtt_client import hub

router = APIRouter(prefix="/api/door", tags=["door"])


class DoorOpenIn(BaseModel):
    device_id: str


@router.post("/open")
async def open_door(body: DoorOpenIn, _u: str = Depends(require_user)):
    if not body.device_id:
        raise HTTPException(status_code=400, detail="device_id required")
    ack = await hub.publish_cmd(body.device_id, "remote_open",
                                wait_ack=True, timeout=6.0)
    return ack
