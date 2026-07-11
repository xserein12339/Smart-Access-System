# 云服务器部署指南

把 Web 控制台 + EMQX broker 部署到你的云服务器，设备通过公网连服务器 broker。

## 拓扑

```
                 公网
ESP32 设备 ───────────────► 云服务器
                            ├─ EMQX broker   :1883/:8883/:18083
                            ├─ FastAPI 后端  :8000（内网，反代）
                            └─ nginx 前端    :8080（对外）
                                  ▲
                          浏览器(手机/PC) ──┘
```

- 设备 ←MQTT→ EMQX ←MQTT→ 后端（同一服务器内网通信）
- 浏览器 ←HTTP/WS→ nginx:8080 →（反代）→ 后端:8000

## 前置条件

- 一台云服务器（Linux，2C2G 起步，EMQX 约占 500MB）
- 服务器公网 IP（下文记为 `<SERVER_IP>`）
- 能 ssh 登录服务器

## 步骤 1：上传代码到服务器

在本地：

```bash
cd software_engineering
# 排除 build 产物与 node_modules
rsync -avz --exclude='frontend/node_modules' \
            --exclude='frontend/dist' \
            --exclude='backend/data' \
            --exclude='backend/.venv' \
            --exclude='backend/__pycache__' \
            web/ <user>@<SERVER_IP>:~/face_access_web/
```

或用 scp 打包上传。

## 步骤 2：服务器初始化（装 Docker、放端口）

ssh 登录服务器：

```bash
ssh <user>@<SERVER_IP>
cd ~/face_access_web
bash scripts/server-init.sh
```

脚本会：检测/安装 Docker、放本机防火墙端口。

**还需手动在云服务商控制台安全组放行端口**：1883、8883、18083、8000、8080（阿里云/腾讯云/AWS 都需单独配置安全组，本机防火墙不够）。

> 安全建议：18083（EMQX Dashboard）和 8000（后端直连）建议只对内网/你的 IP 开放，对外只开 8080（前端）和 1883/8883（设备 MQTT）。

## 步骤 3：一键部署

```bash
bash scripts/deploy.sh
```

脚本会：生成随机 JWT 密钥、配置 CORS 为服务器 IP、`docker compose up -d --build`。

完成后访问：
- 前端：`http://<SERVER_IP>:8080`
- EMQX Dashboard：`http://<SERVER_IP>:18083`（账号在 `backend/.env` 的 `EMQX_ADMIN_USER/PASS`，默认 admin/public，**登录后立即改密**）

## 步骤 4：在 EMQX 创建设备 MQTT 账号

EMQX 已配置 `ALLOW_ANONYMOUS=false`（强制账号密码）。设备要连 broker 必须先建账号：

1. 浏览器打开 `http://<SERVER_IP>:18083`，登录
2. **Authentication** → New → 选 `Password-Based` → Built-in Database → 创建
3. 在该认证器下 **Users** → Add：填用户名/密码（这组凭证给设备用）
4. （多设备场景）可建多个账号，或共用一组

## 步骤 5：配置设备并验证

设备 menuconfig 填入服务器 broker 信息：

```bash
cd firmware
idf.py menuconfig
# → Face Access
#   FACE_DEVICE_ID      = FACE-AABBCC
#   FACE_MQTT_BROKER_URI = mqtt://<SERVER_IP>:1883
#   FACE_MQTT_USERNAME  = 步骤4建的账号
#   FACE_MQTT_PASSWORD  = 步骤4建的密码
idf.py build flash monitor
```

串口应见 `SVC_MQTT: connected to broker`。

浏览器开 `http://<SERVER_IP>:8080`，登录后：
- 仪表盘应显示设备在线
- 远程开门 → 门锁动作 + 回执

## 日常运维

```bash
cd ~/face_access_web

docker compose ps              # 查看状态
docker compose logs -f backend # 看后端日志
docker compose logs -f emqx    # 看 broker 日志
docker compose restart backend # 重启后端
docker compose down            # 停止全部
docker compose up -d --build   # 更新代码后重新构建
```

数据持久化：
- 后端 SQLite：`backend/data/`（docker volume）
- EMQX 数据：docker volume `emqx_data`

## 安全加固（生产必读）

| 项 | 当前 | 建议 |
|---|---|---|
| MQTT 明文 1883 | 开 | 生产关闭，设备改用 8883 TLS（需在 EMQX 配置证书） |
| EMQX Dashboard 18083 | 公网开 | 安全组限制为仅你的 IP / VPN |
| 后端 8000 | 公网开 | 安全组限制为仅内网（前端 nginx 反代即可） |
| ADMIN_PASSWORD | 默认 admin123 | `.env` 改强密码 |
| EMQX_ADMIN_PASS | 默认 public | 登录 Dashboard 后改密 |
| MQTT 凭证传输 | 明文 | 启用 8883 TLS，设备 `FACE_MQTT_BROKER_URI=mqtts://...:8883` |
| 设备 NVS 凭证 | 明文 | 启用 flash encryption + NVS encryption |

## 故障排查

**设备连不上 broker**：
- 服务器安全组是否放行 1883/8883
- EMQX 是否建了设备账号（`ALLOW_ANONYMOUS=false`）
- `docker compose logs emqx` 看拒绝原因

**前端能开但设备离线**：
- 后端是否连上 EMQX：`docker compose logs backend | grep mqtt`
- 设备 device_id 是否与订阅匹配（后端订阅 `face_access/+/evt/#` 通配，理论上不匹配也能收到）

**WebSocket 实时不刷新**：
- 浏览器控制台看 ws 连接是否建立（应为 `ws://<IP>:8080/ws/push?token=...`）
- nginx 的 `/ws/` 反代配置是否生效
