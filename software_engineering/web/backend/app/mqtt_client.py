"""
MQTT 客户端 — Web 后端与设备之间的通信枢纽。

职责：
- 订阅 face_access/+/evt/# 接收设备上行（online/record/recog/fault/cmd_ack/log）
- 解析 topic 提取 device_id，事件落 SQLite + 广播 WebSocket
- 提供 publish_cmd() 下行命令（cmd/remote_open、cmd/user_add、cmd/user_del）
- 维护 msg_id → asyncio.Future，用 evt/cmd_ack 回执唤醒等待的 REST 请求
"""
import asyncio
import json
import logging
import ssl
from datetime import datetime, timezone

import aiomqtt
from sqlmodel import Session, select

from .config import settings
from .db import (DeviceStatus, RecordCache, LogCache, CmdLog, OtaTask, engine)

logger = logging.getLogger("mqtt")

_TOPIC_PREFIX = "face_access/"


class MqttHub:
    def __init__(self) -> None:
        self._client: aiomqtt.Client | None = None
        self._task: asyncio.Task | None = None
        self._running = False
        self._msg_id = 1
        # msg_id -> Future（等待 cmd_ack）
        self._pending: dict[int, asyncio.Future] = {}
        # WebSocket 订阅者回调集合
        self._ws_subscribers: set = set()

    # ---- WebSocket 广播 ----
    def subscribe_ws(self, cb) -> None:
        self._ws_subscribers.add(cb)

    def unsubscribe_ws(self, cb) -> None:
        self._ws_subscribers.discard(cb)

    def _broadcast(self, kind: str, data: dict) -> None:
        for cb in list(self._ws_subscribers):
            try:
                cb({"kind": kind, **data})
            except Exception:  # noqa: BLE001
                logger.exception("ws broadcast failed")

    # ---- 下行命令 ----
    def _next_msg_id(self) -> int:
        self._msg_id += 1
        return self._msg_id

    async def publish_cmd(self, device_id: str, op: str,
                          person_id: int = 0, name: str = "",
                          wait_ack: bool = True, timeout: float = 5.0,
                          extra: dict | None = None) -> dict:
        """下发命令并等待 cmd_ack。返回 {msg_id, code, msg}。extra 用于 OTA 等复杂命令。"""
        if self._client is None:
            raise RuntimeError("mqtt not connected")

        msg_id = self._next_msg_id()
        payload: dict = {"msg_id": msg_id, "op": op}
        if op == "user_add":
            payload["person_id"] = person_id
            payload["name"] = name
        elif op == "user_del":
            payload["person_id"] = person_id
        if extra:
            payload.update(extra)

        topic = f"{_TOPIC_PREFIX}{device_id}/cmd/{op}"
        # 记录命令日志
        with Session(engine) as s:
            s.add(CmdLog(msg_id=msg_id, device_id=device_id, op=op,
                         payload=json.dumps(payload, ensure_ascii=False)))
            s.commit()

        fut: asyncio.Future | None = None
        if wait_ack:
            fut = asyncio.get_event_loop().create_future()
            self._pending[msg_id] = fut

        await self._client.publish(topic, json.dumps(payload, ensure_ascii=False),
                                   qos=1)
        logger.info("cmd sent: %s %s", topic, payload)

        if not wait_ack:
            return {"msg_id": msg_id, "code": -1, "msg": "no_wait"}

        try:
            ack = await asyncio.wait_for(fut, timeout=timeout)
            return ack
        except asyncio.TimeoutError:
            return {"msg_id": msg_id, "code": -2, "msg": "timeout"}

    # ---- 连接与订阅 ----
    async def start(self) -> None:
        self._running = True
        print(f"==== mqtt hub start: broker={settings.mqtt_broker}:{settings.mqtt_port} "
              f"tls={settings.mqtt_use_tls} user={settings.mqtt_username or '(anon)'} ====",
              flush=True)
        self._task = asyncio.create_task(self._run())

    async def stop(self) -> None:
        self._running = False
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass

    async def _run(self) -> None:
        tls_ctx = None
        if settings.mqtt_use_tls:
            tls_ctx = ssl.create_default_context()

        while self._running:
            try:
                async with aiomqtt.Client(
                    hostname=settings.mqtt_broker,
                    port=settings.mqtt_port,
                    username=settings.mqtt_username or None,
                    password=settings.mqtt_password or None,
                    tls_context=tls_ctx,
                    identifier=f"face_access_web_{id(self)}",
                ) as client:
                    self._client = client
                    await client.subscribe(f"{_TOPIC_PREFIX}+/evt/#", qos=1)
                    print("==== mqtt connected, subscribed face_access/+/evt/# ====", flush=True)
                    logger.info("mqtt connected, subscribed face_access/+/evt/#")
                    self._broadcast("hub", {"status": "connected"})

                    async for message in client.messages:
                        try:
                            self._handle_message(message)
                        except Exception:  # noqa: BLE001
                            logger.exception("handle message failed")
            except asyncio.CancelledError:
                break
            except Exception as e:  # noqa: BLE001
                print(f"==== mqtt connection failed: {type(e).__name__}: {e} ====", flush=True)
                logger.exception("mqtt connection lost, reconnect in 5s")
                self._broadcast("hub", {"status": "disconnected"})
                await asyncio.sleep(5)

    def _handle_message(self, message) -> None:
        topic = str(message.topic)
        # topic: face_access/{D}/evt/{sub}
        parts = topic.split("/")
        if len(parts) < 4 or parts[0] != "face_access" or parts[2] != "evt":
            return
        device_id = parts[1]
        sub = parts[3]
        try:
            raw = message.payload.decode()
            data = json.loads(raw)
        except Exception:  # noqa: BLE001
            logger.warning("bad payload on %s: %r", topic, message.payload)
            return

        if sub == "online":
            self._on_online(device_id, data)
        elif sub == "offline":
            self._on_offline(device_id, data)
        elif sub == "record":
            self._on_record(device_id, data)
        elif sub == "recog":
            self._broadcast("recog", {"device_id": device_id, **data})
        elif sub == "fault":
            self._broadcast("fault", {"device_id": device_id, **data})
        elif sub == "cmd_ack":
            self._on_cmd_ack(device_id, data)
        elif sub == "log":
            self._on_log(device_id, data)
        elif sub == "ota_progress":
            self._on_ota_progress(device_id, data)
        elif sub == "ota_result":
            self._on_ota_result(device_id, data)

    # ---- 上行处理 ----
    def _on_online(self, device_id: str, data: dict) -> None:
        with Session(engine) as s:
            row = s.get(DeviceStatus, device_id)
            if row is None:
                row = DeviceStatus(device_id=device_id)
            row.online = True
            row.ip = data.get("ip", "")
            row.uptime_s = data.get("uptime_s", 0)
            row.fw = data.get("fw", "")
            row.db_count = data.get("db_count", 0)
            row.last_seen = datetime.now(timezone.utc)
            s.add(row)
            s.commit()
        self._broadcast("online", {"device_id": device_id, **data})

    def _on_offline(self, device_id: str, data: dict) -> None:
        with Session(engine) as s:
            row = s.get(DeviceStatus, device_id)
            if row is None:
                row = DeviceStatus(device_id=device_id)
            row.online = False
            row.last_seen = datetime.now(timezone.utc)
            s.add(row)
            s.commit()
        self._broadcast("offline", {"device_id": device_id, **data})

    def _on_record(self, device_id: str, data: dict) -> None:
        with Session(engine) as s:
            s.add(RecordCache(
                device_id=device_id,
                person_id=data.get("person_id", 0),
                timestamp=data.get("timestamp", 0),
                result=data.get("result", 0),
                method=data.get("method", 0),
            ))
            s.commit()
        self._broadcast("record", {"device_id": device_id, **data})

    def _on_log(self, device_id: str, data: dict) -> None:
        logs = data.get("logs", [])
        with Session(engine) as s:
            for lg in logs:
                s.add(LogCache(
                    device_id=device_id,
                    lv=lg.get("lv", 2),
                    module=lg.get("mod", ""),
                    ts=lg.get("ts", 0),
                    msg=lg.get("msg", ""),
                ))
            s.commit()
        self._broadcast("log", {"device_id": device_id, "logs": logs})

    def _on_ota_progress(self, device_id: str, data: dict) -> None:
        msg_id = data.get("msg_id", 0)
        percent = data.get("percent", 0)
        with Session(engine) as s:
            row = s.exec(select(OtaTask).where(OtaTask.msg_id == msg_id)).first()
            if row:
                row.percent = percent
                row.status = "downloading"
                if percent >= 100:
                    row.status = "success"
                s.add(row); s.commit()
        self._broadcast("ota_progress", {"device_id": device_id, **data})

    def _on_ota_result(self, device_id: str, data: dict) -> None:
        msg_id = data.get("msg_id", 0)
        code = data.get("code", -1)
        with Session(engine) as s:
            row = s.exec(select(OtaTask).where(OtaTask.msg_id == msg_id)).first()
            if row:
                row.status = "success" if code == 0 else "failed"
                row.finished_at = datetime.now(timezone.utc)
                s.add(row); s.commit()
        self._broadcast("ota_result", {"device_id": device_id, **data})

    def _on_cmd_ack(self, device_id: str, data: dict) -> None:
        msg_id = data.get("msg_id", 0)
        code = data.get("code", -1)
        msg = data.get("msg", "")
        # 更新命令日志
        with Session(engine) as s:
            stmt = select(CmdLog).where(CmdLog.msg_id == msg_id)
            row = s.exec(stmt).first()
            if row is not None:
                row.ack_code = code
                row.ack_msg = msg
                row.acked_at = datetime.now(timezone.utc)
                s.add(row)
                s.commit()
        # 唤醒等待的 REST 请求
        fut = self._pending.pop(msg_id, None)
        if fut is not None and not fut.done():
            fut.set_result({"msg_id": msg_id, "code": code, "msg": msg})
        self._broadcast("cmd_ack", {"device_id": device_id, **data})


hub = MqttHub()
