"""固件管理路由 — 上传/下载/列表/OTA下发"""
import os
import hashlib
import shutil

from fastapi import APIRouter, Depends, HTTPException, UploadFile, File, Query
from fastapi.responses import FileResponse
from pydantic import BaseModel
from sqlmodel import Session, select, desc

from ..auth import require_user
from ..db import FirmwareVersion, OtaTask, get_session, engine
from ..mqtt_client import hub
from ..config import settings

router = APIRouter(prefix="/api", tags=["firmware"])

# 固件存储目录
_FW_DIR = os.path.join(os.path.dirname(settings.db_path), "firmware")
os.makedirs(_FW_DIR, exist_ok=True)


def _sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(8192):
            h.update(chunk)
    return h.hexdigest()


@router.post("/firmware/upload")
async def fw_upload(file: UploadFile = File(...), _u: str = Depends(require_user)):
    """上传固件 .bin 文件"""
    if not file.filename or not file.filename.endswith(".bin"):
        raise HTTPException(400, "only .bin files accepted")

    # 保存文件
    path = os.path.join(_FW_DIR, file.filename)
    with open(path, "wb") as f:
        shutil.copyfileobj(file.file, f)

    size = os.path.getsize(path)
    sha = _sha256_file(path)

    # 从文件名提取版本号（或从 URL 参数）
    ver = file.filename.replace(".bin", "").lstrip("v")

    with Session(engine) as s:
        existing = s.exec(select(FirmwareVersion).where(
            FirmwareVersion.version == ver)).first()
        if existing:
            # 覆盖旧版本
            existing.filename = file.filename
            existing.size = size
            existing.sha256 = sha
            existing.uploaded_at = None  # 让 default 生效
        else:
            s.add(FirmwareVersion(version=ver, filename=file.filename,
                                   size=size, sha256=sha))
        s.commit()

    return {"version": ver, "filename": file.filename, "size": size, "sha256": sha}


@router.get("/firmware")
def fw_list(s: Session = Depends(get_session), _u: str = Depends(require_user)):
    """列出所有固件版本"""
    rows = s.exec(select(FirmwareVersion).order_by(desc(FirmwareVersion.id))).all()
    return [{"id": r.id, "version": r.version, "filename": r.filename,
             "size": r.size, "sha256": r.sha256,
             "uploaded_at": r.uploaded_at.isoformat() if r.uploaded_at else None}
            for r in rows]


@router.delete("/firmware/{fw_id}")
def fw_delete(fw_id: int, s: Session = Depends(get_session),
              _u: str = Depends(require_user)):
    """删除固件版本"""
    r = s.get(FirmwareVersion, fw_id)
    if r is None:
        raise HTTPException(404, "not found")
    path = os.path.join(_FW_DIR, r.filename)
    if os.path.exists(path):
        os.remove(path)
    s.delete(r)
    s.commit()
    return {"ok": True}


@router.get("/firmware/download/{filename}")
def fw_download(filename: str):
    """设备 HTTP 下载固件（无需鉴权）"""
    # 安全检查：防止路径穿越
    if ".." in filename or "/" in filename:
        raise HTTPException(400, "invalid filename")
    path = os.path.join(_FW_DIR, filename)
    if not os.path.exists(path):
        raise HTTPException(404, "file not found")
    return FileResponse(path, media_type="application/octet-stream",
                        filename=filename)


class OtaStartIn(BaseModel):
    device_id: str
    firmware_id: int


@router.post("/ota/start")
async def ota_start(body: OtaStartIn, s: Session = Depends(get_session),
                     _u: str = Depends(require_user)):
    """下发 OTA 命令到设备"""
    fw = s.get(FirmwareVersion, body.firmware_id)
    if fw is None:
        raise HTTPException(404, "firmware not found")

    # 构建下载 URL：设备从服务器拉取固件
    # 服务器 IP 从请求/配置推断。设备在同一个网络，用内网 IP 或公网 IP 均可
    download_url = f"http://47.109.76.87:8000/api/firmware/download/{fw.filename}"

    ack = await hub.publish_cmd(
        body.device_id, "ota_start",
        wait_ack=False,  # OTA 不阻塞等 ACK，走 WebSocket 进度推送
        extra={
            "version": fw.version,
            "size": fw.size,
            "sha256": fw.sha256,
            "url": download_url,
        },
    )

    # 记录任务
    s.add(OtaTask(device_id=body.device_id, firmware_id=fw.id,
                  msg_id=ack["msg_id"], status="pending"))
    s.commit()

    return ack
