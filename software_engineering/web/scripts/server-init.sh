#!/usr/bin/env bash
# 云服务器初始化：检测/安装 Docker，放防火墙端口
# 在云服务器上以 root 或 sudo 执行：bash server-init.sh
set -e

echo "==== 1. 检测 Docker ===="
if command -v docker &>/dev/null; then
    echo "Docker 已安装：$(docker --version)"
else
    echo "Docker 未安装，开始安装..."
    curl -fsSL https://get.docker.com | sh
    systemctl enable --now docker
    echo "Docker 安装完成：$(docker --version)"
fi

echo "==== 2. 检测 docker compose 插件 ===="
if docker compose version &>/dev/null; then
    echo "docker compose 可用：$(docker compose version)"
else
    echo "ERROR: docker compose 插件缺失，请安装 docker-compose-plugin"
    exit 1
fi

echo "==== 3. 放防火墙端口 ===="
# 端口用途：
#   1883  MQTT（设备，明文；建议生产改 8883 TLS）
#   8883  MQTT over TLS（设备生产用）
#   8083  MQTT over WebSocket（可选）
#   18083 EMQX Dashboard（管理后台，建议只对内网/VPN 开放）
#   8000  后端 API（可只对内网，由前端 nginx 反代）
#   8080  前端 Web（对外访问）
PORTS=(1883 8883 8083 18083 8000 8080)

if command -v ufw &>/dev/null; then
    echo "使用 ufw 放行端口..."
    for p in "${PORTS[@]}"; do
        ufw allow "$p"/tcp || true
    done
    echo "ufw 状态："; ufw status | head -20
elif command -v firewall-cmd &>/dev/null; then
    echo "使用 firewalld 放行端口..."
    for p in "${PORTS[@]}"; do
        firewall-cmd --permanent --add-port="$p"/tcp || true
    done
    firewall-cmd --reload
    echo "firewalld 已放行：${PORTS[*]}"
else
    echo "WARN: 未检测到 ufw/firewalld，请在云服务器控制台安全组放行端口：${PORTS[*]}"
fi

echo ""
echo "==== 重要：云服务器安全组 ===="
echo "除本机防火墙外，还需在云服务商控制台（阿里云/腾讯云/AWS 安全组）放行上述端口。"
echo ""
echo "==== 完成 ===="
echo "下一步：把 web/ 目录上传到服务器，进入该目录执行："
echo "  cp backend/.env.example backend/.env && 编辑 backend/.env"
echo "  docker compose up -d --build"
