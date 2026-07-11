"""人员管理路由 — 下发 user_add/user_del 命令到设备"""
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from ..auth import require_user
from ..mqtt_client import hub

router = APIRouter(prefix="/api/persons", tags=["persons"])


class PersonAddIn(BaseModel):
    device_id: str
    person_id: int
    name: str


class PersonDelIn(BaseModel):
    device_id: str
    person_id: int


@router.post("")
async def add_person(body: PersonAddIn, _u: str = Depends(require_user)):
    if not body.device_id or body.person_id <= 0:
        raise HTTPException(status_code=400, detail="device_id/person_id required")
    ack = await hub.publish_cmd(body.device_id, "user_add",
                                person_id=body.person_id, name=body.name,
                                wait_ack=True, timeout=6.0)
    return ack


@router.delete("")
async def del_person(body: PersonDelIn, _u: str = Depends(require_user)):
    if not body.device_id or body.person_id <= 0:
        raise HTTPException(status_code=400, detail="device_id/person_id required")
    ack = await hub.publish_cmd(body.device_id, "user_del",
                                person_id=body.person_id,
                                wait_ack=True, timeout=6.0)
    return ack
