# WT32 Dashboard Gateway

这是 WT32-SC01 仪表盘的 Mock-first 后端初版。它固定输出适合嵌入式客户端消费的紧凑、带版本数据契约；目前不会连接 PVE、群晖、Home Assistant 或外部 API。

## 接口

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/healthz` | 存活状态与运行时间 |
| GET | `/v1/bootstrap` | 设备配置、8 个页面的顺序、能力声明 |
| GET | `/v1/snapshot` | 10 个数据分区的一次性快照（含保留的 `clock` 和通知数据） |
| POST | `/v1/home/commands` | 执行白名单内的 Mock 智能家居命令 |
| POST | `/v1/mock/tick` | 推进 Mock revision 并制造轻微数据变化 |

所有响应使用同一个顶层 envelope：

```json
{
  "schema_version": "1.0",
  "revision": 1,
  "generated_at": "2026-08-14T22:18:00+08:00",
  "status": "fresh",
  "data": {},
  "error": null
}
```

`status` 只会是 `fresh`、`stale`、`error`、`unsupported`。`snapshot` 中每个数据源另有独立状态、更新时间和过期阈值，因此未来真实连接器可以局部失败。

Antigravity 数据源明确返回 `unsupported`，因为尚未配置受支持的机器可读额度 API。股票和油价未配置 provider 时明确返回 `unsupported`，不会生成假数值。

## 本地运行

需要 Python 3.11 或更新版本：

```bash
cd gateway
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements-test.lock
cp .env.example .env
# 编辑 .env，为 GATEWAY_API_TOKEN 填入至少 16 字符的随机令牌
uvicorn app.main:app --reload --host 0.0.0.0 --port 8080
```

打开 `http://127.0.0.1:8080/docs` 查看 OpenAPI 文档。

## macOS 后台服务

要让 Gateway 在终端和 Codex 任务结束后继续运行，使用项目附带的 LaunchAgent。它只监听本机 `127.0.0.1:8080`，不会开放到局域网；日志保存在 `gateway/logs/`。

```bash
cd gateway
./scripts/setup-local-runtime.sh
./scripts/install-local-service.sh
./scripts/status-local-service.sh
```

浏览器地址：

- API 文档：`http://127.0.0.1:8080/docs`
- 健康检查：`http://127.0.0.1:8080/healthz`

停止并彻底移除 LaunchAgent：

```bash
cd gateway
./scripts/uninstall-local-service.sh
```

安装脚本会在 `~/Library/LaunchAgents/com.wt32.dashboard-gateway.plist` 生成可随时删除的用户级配置。因服务仅绑定 loopback，本地预览模式没有配置 API Token；后续给 WT32 开放局域网访问时必须改为鉴权配置。

## Docker Compose

```bash
cd gateway
cp .env.example .env
# 编辑 .env，为 GATEWAY_API_TOKEN 填入至少 16 字符的随机令牌
docker compose up --build
```

容器对外端口由 `GATEWAY_PORT` 控制，容器内固定监听 8080。

## 智能家居命令

两个 POST 接口使用 `X-API-Token` 保护，令牌来自 `GATEWAY_API_TOKEN`，长度至少 16 个字符。Docker 镜像默认设置 `GATEWAY_REQUIRE_AUTH=true`，缺少令牌时会拒绝启动。直接运行 Python 且未配置令牌时会进入仅供本机开发的无鉴权模式并打印警告，不应以此方式监听家庭局域网。

家居命令只允许 `/v1/snapshot` 的 `home.data.entities` 中列出的 `entity_id` 和 `allowed_actions`，每种动作的参数名及数值范围也有白名单。`request_id` 是幂等键；完全相同的重复请求不会再次执行，并返回原确认结果且 `duplicate=true`。同一 `request_id` 携带不同命令会返回 `request_id_conflict`，调用方必须生成新的 ID。

```bash
curl -X POST http://127.0.0.1:8080/v1/home/commands \
  -H 'Content-Type: application/json' \
  -H 'X-API-Token: replace-with-a-random-token' \
  -d '{
    "request_id": "wt32-0001",
    "entity_id": "light.living_room",
    "action": "set_brightness",
    "parameters": {"brightness_percent": 60}
  }'
```

## 测试

```bash
cd gateway
pytest
```

测试覆盖 envelope、页面清单、Antigravity unsupported 状态、revision 推进、命令确认/幂等、实体白名单和参数边界。
