# 人脸门禁 IoT — MQTT 协议契约

本文件是 **Web 后端**与**设备固件 `svc_mqtt`** 之间的共享通信契约。两端必须严格遵循。

## 1. 总则

- `{D}` = `device_id`，每台设备唯一稳定字符串（如 `FACE-AABBCC`）。
- 传输层：MQTT v3.1.1。broker = 云服务器自建 EMQX（`docker-compose.yml`），强制账号密码鉴权（`ALLOW_ANONYMOUS=false`）。生产建议 8883 TLS。
- `msg_id`：`uint32`。**下行**命令由 Web 生成，设备在 `cmd_ack` 中原样回传用于关联；**上行**事件由设备单调递增生成，供 Web 去重。
- `timestamp`：Unix 秒。设备未同步时间时填 0。
- 所有 payload 为 UTF-8 JSON，紧凑无注释。

## 2. 主题命名空间

```
face_access/{D}/cmd/remote_open     下行  QoS1
face_access/{D}/cmd/user_add        下行  QoS1
face_access/{D}/cmd/user_del        下行  QoS1
face_access/{D}/evt/online          上行  QoS0   设备定时心跳
face_access/{D}/evt/offline         上行  QoS1   MQTT LWT（遗嘱）
face_access/{D}/evt/record          上行  QoS1   通行记录
face_access/{D}/evt/recog           上行  QoS1   人脸识别结果
face_access/{D}/evt/fault           上行  QoS1   故障/告警
face_access/{D}/evt/cmd_ack         上行  QoS1   命令执行回执
face_access/{D}/evt/log             上行  QoS1   日志批次（Phase2）
face_access/{D}/sync/db             上行  QoS2   全量人员库同步（Phase2）
```

设备订阅 `face_access/{D}/cmd/#`；Web 订阅 `face_access/+/evt/#`（通配所有设备）。

## 3. 下行命令（Web → 设备）

### 3.1 远程开门
Topic: `face_access/{D}/cmd/remote_open`
```json
{"msg_id": 123, "op": "remote_open"}
```

### 3.2 新增人员
Topic: `face_access/{D}/cmd/user_add`
```json
{"msg_id": 124, "op": "user_add", "person_id": 5, "name": "Alice"}
```
- `name` 最长 31 字符（+ NUL = 32）。本期不带人脸特征（占位零特征，Phase2 扩 `feature` base64 字段）。

### 3.3 删除人员
Topic: `face_access/{D}/cmd/user_del`
```json
{"msg_id": 125, "op": "user_del", "person_id": 5}
```

## 4. 上行事件（设备 → Web）

### 4.1 在线心跳
Topic: `face_access/{D}/evt/online`  QoS0
```json
{"device_id": "FACE-AABBCC", "online": true, "ip": "192.168.1.10",
 "uptime_s": 1234, "fw": "1.0.0", "db_count": 42}
```

### 4.2 离线（遗嘱）
Topic: `face_access/{D}/evt/offline`  QoS1
```json
{"device_id": "FACE-AABBCC", "online": false}
```

### 4.3 通行记录
Topic: `face_access/{D}/evt/record`  QoS1
```json
{"msg_id": 7, "person_id": 5, "timestamp": 1700000000, "result": 0, "method": 1}
```

### 4.4 识别结果
Topic: `face_access/{D}/evt/recog`  QoS1
```json
{"msg_id": 8, "person_id": 5, "confidence": 0.92, "quality": 0.8,
 "liveness": true, "timestamp": 1700000000}
```

### 4.5 故障
Topic: `face_access/{D}/evt/fault`  QoS1
```json
{"msg_id": 9, "code": 3, "msg": "cam_timeout", "timestamp": 1700000000}
```

### 4.6 命令回执
Topic: `face_access/{D}/evt/cmd_ack`  QoS1
```json
{"msg_id": 123, "code": 0, "msg": "ok"}
```

### 4.7 日志批次（Phase2）
Topic: `face_access/{D}/evt/log`  QoS1
```json
{"msg_id": 10, "logs": [{"lv": 2, "mod": "MQTT", "ts": 1700000000, "msg": "connected"}]}
```

## 5. 枚举值

| 字段 | 值 | 含义 |
|---|---|---|
| `result` | 0/1/2 | PASS / REJECT / ERROR |
| `method` | 0/1/2 | 人脸 / 远程开门 / 其它 |
| `cmd_ack.code` | 0/1/2/3/4 | ok / rejected_by_policy / db_error / unknown_cmd / dup_msg_id |
| `lv`（日志） | 0/1/2/3 | ERROR / WARN / INFO / DEBUG |

## 6. QoS 策略

- QoS0：`online` 心跳——丢一两条无影响，靠下次心跳补偿。
- QoS1：命令、`cmd_ack`、`record`、`recog`、`fault`、`log`——必须可达，靠 esp-mqtt 内置 outbox 离线缓存与重传。
- QoS2：`sync/db` 全量同步——精确一次。

## 7. 安全约束

broker 跑在云服务器，任何人若拿到设备凭证即可订阅/伪造。务必：
1. EMQX 关闭匿名登录（`ALLOW_ANONYMOUS=false`，docker-compose 已配），在 Dashboard 建独立设备账号。
2. 每台设备独立凭证，主题按 `device_id` 隔离。
3. 下行命令带 `msg_id`，Web 校验 `cmd_ack` 回执，超时告警。
4. 生产环境关闭 1883 明文端口，设备改用 8883 TLS（EMQX 配置证书）；EMQX Dashboard 与后端端口仅对内网开放。
5. 设备侧开启 flash encryption + NVS encryption 保护凭证。

详见 [DEPLOY-SERVER.md](DEPLOY-SERVER.md) 安全加固章节。
