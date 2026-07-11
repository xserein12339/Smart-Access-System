"""
WebSocket 实时推送 — /ws/push
向前端推送 record/log/online/offline/recog/fault/cmd_ack 事件。
"""
import asyncio
import json
import logging

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from .auth import create_token  # noqa: F401  (复用)
from .mqtt_client import hub

logger = logging.getLogger("ws")
router = APIRouter(tags=["ws"])


@router.websocket("/ws/push")
async def ws_push(ws: WebSocket):
    """WebSocket 连接。鉴权：query 参数 ?token=<jwt>。"""
    token = ws.query_params.get("token", "")
    # 校验 token
    try:
        import jwt as _jwt
        from .config import settings
        _jwt.decode(token, settings.jwt_secret, algorithms=[settings.jwt_alg])
    except Exception:  # noqa: BLE001
        await ws.close(code=4401)
        return

    await ws.accept()

    queue: asyncio.Queue = asyncio.Queue()

    def cb(msg: dict) -> None:
        try:
            queue.put_nowait(msg)
        except asyncio.QueueFull:  # noqa: BLE001
            pass

    hub.subscribe_ws(cb)
    try:
        while True:
            msg = await queue.get()
            await ws.send_text(json.dumps(msg, ensure_ascii=False))
    except WebSocketDisconnect:
        pass
    except Exception:  # noqa: BLE001
        logger.exception("ws error")
    finally:
        hub.unsubscribe_ws(cb)
