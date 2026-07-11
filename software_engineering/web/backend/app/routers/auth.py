"""鉴权路由 — POST /api/login"""
from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel
from sqlmodel import Session, select

from ..auth import create_token, verify_password
from ..auth import require_user
from ..db import WebUser, get_session

router = APIRouter(prefix="/api", tags=["auth"])


class LoginIn(BaseModel):
    username: str
    password: str


class LoginOut(BaseModel):
    token: str
    username: str


@router.post("/login", response_model=LoginOut)
def login(body: LoginIn, s: Session = Depends(get_session)):
    user = s.exec(select(WebUser).where(
        WebUser.username == body.username)).first()
    if user is None or not verify_password(body.password, user.password_hash):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED,
                            detail="invalid credentials")
    return LoginOut(token=create_token(user.username), username=user.username)


@router.get("/me")
def me(user: str = Depends(require_user)):
    return {"username": user}
