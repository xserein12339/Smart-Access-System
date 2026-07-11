#!/usr/bin/env bash
# 部署脚本：在 web/ 目录执行
#   1. 生成随机 JWT_SECRET（如未设置）
#   2. 生成 EMQX 设备账号（如未设置）— 写入 emqx-users.txt 供你在 EMQX Dashboard 录入
#   3. 构建并启动所有服务
set -e

cd "$(dirname "$0")/.."

echo "==== 1. 检查 backend/.env ===="
if [ ! -f backend/.env ]; then
    cp backend/.env.example backend/.env
    SECRET=$(openssl rand -hex 32)
    # 替换 JWT_SECRET
    sed -i "s|^JWT_SECRET=.*|JWT_SECRET=$SECRET|" backend/.env
    echo "已生成 backend/.env 并写入随机 JWT_SECRET"
else
    echo "backend/.env 已存在，跳过（如需重置请先删除）"
fi

SERVER_IP=$(curl -s --max-time 3 ifconfig.me || echo "<SERVER_IP>")
echo "检测到服务器公网 IP：$SERVER_IP"
# 把 CORS 加上服务器 IP
if grep -q "$SERVER_IP" backend/.env; then
    :
else
    sed -i "s|^CORS_ORIGINS=.*|CORS_ORIGINS=http://$SERVER_IP:8080,http://localhost:8080|" backend/.env
    echo "已把 http://$SERVER_IP:8080 加入 CORS_ORIGINS"
fi

echo ""
echo "==== 2. 构建并启动 ===="
docker compose up -d --build

echo ""
echo "==== 3. 等待服务就绪 ===="
sleep 8
docker compose ps

echo ""
echo "==== 部署完成 ===="
echo "前端 Web    : http://$SERVER_IP:8080"
echo "后端 API    : http://$SERVER_IP:8000/api/health"
echo "EMQX Dashboard: http://$SERVER_IP:18083  (账号见 backend/.env 的 EMQX_ADMIN_USER/PASS)"
echo ""
echo "==== 下一步：在 EMQX 创建设备 MQTT 账号 ===="
echo "1. 浏览器打开 http://$SERVER_IP:18083 登录"
echo "2. Authentication → 新建 Password-Based 认证器"
echo "3. 添加用户：用户名/密码即设备 svc_mqtt 用的 MQTT_USERNAME/PASSWORD"
echo "4. 设备 menuconfig 填：broker=服务器IP, port=1883(或8883 TLS), user/pass=刚建的账号, device_id=FACE-xxxx"
echo ""
echo "⚠️  安全建议：生产环境关闭 1883 明文端口，设备改用 8883(TLS)；18083 Dashboard 仅对内网开放。"
