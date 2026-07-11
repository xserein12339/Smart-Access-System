"""
JWT 鉴权 — 登录签发、依赖注入校验、密码哈希。
"""
import hashlib
import hmac
import time
import jwt
from fastapi import Depends, HTTPException, status
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials

from .config import settings

bearer = HTTPBearer()


def hash_password(pw: str) -> str:
    """简单 SHA256 哈希（演示用，生产应换 bcrypt/argon2）"""
    return hashlib.sha256(pw.encode()).hexdigest()


def verify_password(pw: str, pw_hash: str) -> bool:
    return hmac.compare_digest(hash_password(pw), pw_hash)


def create_token(username: str) -> str:
    now = int(time.time())
    payload = {
        "sub": username,
        "iat": now,
        "exp": now + settings.jwt_expire_hours * 3600,
    }
    return jwt.encode(payload, settings.jwt_secret, algorithm=settings.jwt_alg)


def require_user(
    cred: HTTPAuthorizationCredentials = Depends(bearer),
) -> str:
    """FastAPI 依赖：校验 JWT，返回用户名"""
    try:
        payload = jwt.decode(cred.credentials, settings.jwt_secret,
                             algorithms=[settings.jwt_alg])
        return payload.get("sub", "")
    except jwt.PyJWTError:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED,
                            detail="invalid token")
