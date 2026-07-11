"""
配置加载 — 从环境变量/.env 读取运行参数。
"""
import os
from dataclasses import dataclass, field
from dotenv import load_dotenv

load_dotenv()


@dataclass
class Settings:
    jwt_secret: str = os.getenv("JWT_SECRET", "change-me")
    jwt_alg: str = os.getenv("JWT_ALG", "HS256")
    jwt_expire_hours: int = int(os.getenv("JWT_EXPIRE_HOURS", "24"))

    admin_username: str = os.getenv("ADMIN_USERNAME", "admin")
    admin_password: str = os.getenv("ADMIN_PASSWORD", "admin123")

    mqtt_broker: str = os.getenv("MQTT_BROKER", "broker.emqx.io")
    mqtt_port: int = int(os.getenv("MQTT_PORT", "8883"))
    mqtt_username: str = os.getenv("MQTT_USERNAME", "")
    mqtt_password: str = os.getenv("MQTT_PASSWORD", "")
    mqtt_use_tls: bool = os.getenv("MQTT_USE_TLS", "true").lower() == "true"

    device_ids: list[str] = field(default_factory=lambda: [
        d.strip() for d in os.getenv("DEVICE_IDS", "").split(",") if d.strip()
    ])

    db_path: str = os.getenv("DB_PATH", "data/face_access.db")
    cors_origins: list[str] = field(default_factory=lambda: [
        o.strip() for o in os.getenv("CORS_ORIGINS", "http://localhost:5173").split(",")
        if o.strip()
    ])


settings = Settings()
