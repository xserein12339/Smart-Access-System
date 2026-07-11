# 运行阶段：直接用本地预构建的 dist，避免服务器跑 npm build（内存不足）
# 使用前需先在本地执行 `npm run build` 生成 frontend/dist/
FROM nginx:alpine
COPY dist/ /usr/share/nginx/html/
COPY demo/ /usr/share/nginx/html/demo/
COPY nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80 8090
