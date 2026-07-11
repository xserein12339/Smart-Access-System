"""
SQLite + SQLModel 数据模型。
存 Web 侧：管理员账号、设备状态缓存、通行记录缓存、远程日志缓存、命令日志。
"""
import os
from datetime import datetime, timezone
from sqlmodel import SQLModel, Field, Session, create_engine, select

from .config import settings

os.makedirs(os.path.dirname(settings.db_path) or ".", exist_ok=True)
engine = create_engine(
    f"sqlite:///{settings.db_path}",
    connect_args={"check_same_thread": False},
    echo=False,
)


class WebUser(SQLModel, table=True):
    """Web 管理员账号"""
    id: int | None = Field(default=None, primary_key=True)
    username: str = Field(index=True, unique=True)
    password_hash: str


class DeviceStatus(SQLModel, table=True):
    """设备在线状态缓存（来自 evt/online、evt/offline）"""
    device_id: str = Field(primary_key=True)
    online: bool = False
    ip: str = ""
    uptime_s: int = 0
    fw: str = ""
    db_count: int = 0
    last_seen: datetime | None = Field(default=None)


class RecordCache(SQLModel, table=True):
    """通行记录缓存（来自 evt/record）"""
    id: int | None = Field(default=None, primary_key=True)
    device_id: str = Field(index=True)
    person_id: int = 0
    timestamp: int = 0
    result: int = 0       # 0=PASS 1=REJECT 2=ERROR
    method: int = 0       # 0=face 1=remote 2=other
    received_at: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))


class LogCache(SQLModel, table=True):
    """远程日志缓存（来自 evt/log）"""
    id: int | None = Field(default=None, primary_key=True)
    device_id: str = Field(index=True)
    lv: int = 2           # 0=ERROR 1=WARN 2=INFO 3=DEBUG
    module: str = ""
    ts: int = 0
    msg: str = ""
    received_at: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))


class CmdLog(SQLModel, table=True):
    """下发命令日志（含 cmd_ack 回执）"""
    id: int | None = Field(default=None, primary_key=True)
    msg_id: int = Field(index=True)
    device_id: str = Field(index=True)
    op: str               # remote_open/user_add/user_del
    payload: str = ""
    sent_at: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))
    ack_code: int | None = Field(default=None)   # None=未回执
    ack_msg: str = ""
    acked_at: datetime | None = Field(default=None)


class FirmwareVersion(SQLModel, table=True):
    """上传的固件版本"""
    id: int | None = Field(default=None, primary_key=True)
    version: str = Field(index=True, unique=True)  # "2.0.0"
    filename: str                                    # "v2.0.0.bin"
    size: int = 0                                    # bytes
    sha256: str = ""                                  # hex
    uploaded_at: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))


class OtaTask(SQLModel, table=True):
    """OTA 升级任务记录"""
    id: int | None = Field(default=None, primary_key=True)
    device_id: str = Field(index=True)
    firmware_id: int = 0
    msg_id: int = 0
    status: str = "pending"  # pending/downloading/success/failed
    percent: int = 0
    created_at: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))
    finished_at: datetime | None = Field(default=None)


def init_db() -> None:
    """建表 + 写入默认管理员"""
    SQLModel.metadata.create_all(engine)
    # 默认管理员
    from .auth import hash_password
    with Session(engine) as s:
        admin = s.exec(select(WebUser).where(
            WebUser.username == settings.admin_username)).first()
        if admin is None:
            s.add(WebUser(username=settings.admin_username,
                          password_hash=hash_password(settings.admin_password)))
            s.commit()


def get_session():
    with Session(engine) as s:
        yield s
