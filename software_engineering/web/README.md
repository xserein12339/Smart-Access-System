# 人脸门禁 IoT Web 控制台

远程开关门、远程日志查看、远程用户管理。手机/电脑通用（响应式）。

- 通信契约：[docs/PROTOCOL.md](docs/PROTOCOL.md)
- **云服务器部署（主路径）**：[docs/DEPLOY-SERVER.md](docs/DEPLOY-SERVER.md)

## 架构

```
                 公网
ESP32 设备 ───────────────► 云服务器
                            ├─ EMQX broker   :1883/:8883/:18083
                            ├─ FastAPI 后端  :8000（反代）
                            └─ nginx 前端    :8080（对外）
                                  ▲
                          浏览器(手机/PC) ──┘
```

- **broker**：云服务器自建 EMQX（`docker-compose.yml` 内），强制账号密码鉴权。设备与后端都连这个 broker。
- **后端** (`backend/`)：FastAPI + SQLite + aiomqtt。订阅 `face_access/+/evt/#`，下发 `cmd/*`，提供 REST + WebSocket。
- **前端** (`frontend/`)：Vue 3 + Element Plus，响应式（PC 侧边栏 / 手机抽屉）。
- **固件** (`../firmware/`)：`svc_mqtt`（纯通信管道）+ `svc_perm_manager`（业务消费者）。

## 云服务器部署（推荐）

详细步骤见 [docs/DEPLOY-SERVER.md](docs/DEPLOY-SERVER.md)，简述：

```bash
# 1. 上传 web/ 到服务器
# 2. 服务器初始化（装 Docker、放端口）
bash scripts/server-init.sh
# 3. 一键部署（生成密钥、构建、启动 EMQX+后端+前端）
bash scripts/deploy.sh
# 4. 在 EMQX Dashboard(:18083) 建设备 MQTT 账号
# 5. 设备 menuconfig 填服务器 IP + 账号，flash
```

访问 `http://<SERVER_IP>:8080`，默认账号 `admin` / `.env` 中 `ADMIN_PASSWORD`。

## 本地开发

```bash
# 后端热重载（broker 用同机 docker compose 起的 EMQX）
cd backend
cp .env.example .env
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/uvicorn app.main:app --reload --port 8000

# 前端 dev server（自动代理 /api /ws → 后端）
cd frontend
npm install
npm run dev   # http://localhost:5173
```

本地若要起完整 EMQX：`docker compose up -d emqx`，后端 `.env` 的 `MQTT_BROKER=127.0.0.1`。

## 目录

```
web/
├── docs/
│   ├── PROTOCOL.md          # MQTT 协议契约（两端共享）
│   └── DEPLOY-SERVER.md     # 云服务器部署指南
├── scripts/
│   ├── server-init.sh       # 服务器初始化（装 Docker、放端口）
│   └── deploy.sh            # 一键部署
├── backend/                 # FastAPI 后端
│   ├── app/                 # main/config/db/auth/mqtt_client/ws + routers/
│   ├── requirements.txt / Dockerfile / .env.example
├── frontend/                # Vue3 + Element Plus
│   ├── src/                 # views/ layout/ store/ api.ts router.ts
│   ├── Dockerfile / nginx.conf / package.json
└── docker-compose.yml       # EMQX + 后端 + 前端
```

## Phase2 待办

- USER_ADD 真实人脸特征录入（需固件 `svc_face_feature` + `db_person_update_feature`）
- `evt/log` 日志批次上行（需 `log_sink_drain` 或 `svc_log`）
- 全量人员库同步 `sync/db`（QoS2）
- 设备 provisioning 主题 `prov/set`
- 生产硬化：8883 TLS、flash/NVS encryption、bcrypt 密码哈希

