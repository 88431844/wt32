# 监控刷新间隔实现计划

> **供自动化开发执行者使用：** 必须使用 `superpowers:executing-plans` 按任务逐项执行；每一步使用复选框（`- [ ]`）跟踪。

**目标：** 将群晖 NAS 与 PVE 的刷新设置统一为 5、10、30、60 秒四档，并将默认值改为 5 秒。

**架构：** 保留现有界面到 `app_model_set_refresh_seconds()` 控制队列的数据流。界面和实时 provider 分别使用相同的严格白名单校验持久化值，使旧的 120 秒或其他无效值在内存中回退到 5 秒；现有活动页面轮询和串行调度不变。

**技术栈：** C、ESP-IDF、FreeRTOS、LVGL、Python `unittest` 契约测试。

---

## 文件结构

- 修改 `tests/test_dashboard_ui_contract.py`：验证设置页面选项、界面默认值和旧选项移除。
- 修改 `tests/test_monitor_migration_contract.py`：验证 provider 默认值、合法值白名单和串行周期调度保持不变。
- 修改 `components/dashboard_ui/dashboard_ui.c`：显示四个新选项，并将界面默认值及持久化值校验改为新集合。
- 修改 `components/app_model/live_provider.c`：将 provider 默认值及刷新命令校验改为新集合。

### 任务 1：刷新间隔契约与实现

**文件：**

- 修改：`tests/test_dashboard_ui_contract.py`
- 修改：`tests/test_monitor_migration_contract.py`
- 修改：`components/dashboard_ui/dashboard_ui.c`
- 修改：`components/app_model/live_provider.c`

- [x] **步骤 1：先修改界面契约测试**

将 `test_settings_offer_four_refresh_intervals_and_active_page_polling` 改为验证以下内容：

```python
def test_settings_offer_supported_refresh_intervals_and_default_to_five_seconds(self) -> None:
    self.assertIn("#define REFRESH_OPTION_COUNT 4", self.source)
    for label in ("5 秒", "10 秒", "30 秒", "60 秒"):
        self.assertIn(f'"{label}"', self.source)
    self.assertNotIn('"120 秒"', self.source)
    self.assertIn("static uint8_t s_refresh_seconds = 5", self.source)
    self.assertIn("uint8_t saved_refresh = 5", self.source)
    self.assertIn("else s_refresh_seconds = 5", self.source)
    self.assertIn("DEVICE_KEY_REFRESH", self.source)
    self.assertIn("app_model_set_refresh_seconds", self.source)
    self.assertIn("app_model_set_active_monitor", self.source)
```

- [x] **步骤 2：补充 provider 契约测试**

在 `MonitorMigrationContractTest` 中新增：

```python
def test_provider_supports_same_refresh_intervals_and_defaults_to_five_seconds(self) -> None:
    source = PROVIDER.read_text(encoding="utf-8")
    self.assertIn("#define DEFAULT_REFRESH_SECONDS 5", source)
    self.assertIn(
        "return seconds == 5 || seconds == 10 || seconds == 30 || seconds == 60;",
        source,
    )
    self.assertNotIn("seconds == 120", source)
    self.assertIn("elapsed < interval ? interval - elapsed : 0", source)
```

- [x] **步骤 3：运行目标测试并确认按预期失败**

运行：

```bash
python3 -m unittest \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_settings_offer_supported_refresh_intervals_and_default_to_five_seconds \
  tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_provider_supports_same_refresh_intervals_and_defaults_to_five_seconds -v
```

预期：两个测试均为 `FAIL`；失败原因分别是界面仍使用 10 秒默认值及 `10/30/60/120`，provider 仍使用 10 秒默认值及旧白名单。

- [x] **步骤 4：实现界面刷新选项和回退逻辑**

在 `components/dashboard_ui/dashboard_ui.c` 中将相关代码修改为：

```c
static uint8_t s_refresh_seconds = 5;

static void set_refresh_button_state(void)
{
    static const uint8_t intervals[REFRESH_OPTION_COUNT] = {5, 10, 30, 60};
    for (size_t i = 0; i < REFRESH_OPTION_COUNT; ++i) {
        if (s_ui.refresh_buttons[i] != NULL) {
            set_button_selected(s_ui.refresh_buttons[i], intervals[i] == s_refresh_seconds);
        }
    }
}

static const char *const labels[REFRESH_OPTION_COUNT] = {
    "5 秒", "10 秒", "30 秒", "60 秒"
};
static const uint8_t intervals[REFRESH_OPTION_COUNT] = {5, 10, 30, 60};

uint8_t saved_refresh = 5;
if (device_settings_get_u8(DEVICE_KEY_REFRESH, &saved_refresh) == ESP_OK &&
    (saved_refresh == 5 || saved_refresh == 10 ||
     saved_refresh == 30 || saved_refresh == 60))
    s_refresh_seconds = saved_refresh;
else s_refresh_seconds = 5;
```

- [x] **步骤 5：实现 provider 默认值和白名单**

在 `components/app_model/live_provider.c` 中修改为：

```c
#define DEFAULT_REFRESH_SECONDS 5

static bool valid_refresh_seconds(uint8_t seconds)
{
    return seconds == 5 || seconds == 10 || seconds == 30 || seconds == 60;
}
```

保持 `app_model_set_refresh_seconds()`、启动时 NVS 校验以及现有 `elapsed` 周期计算继续调用该白名单或默认常量。

- [x] **步骤 6：运行目标测试并确认通过**

运行步骤 3 的同一条命令。

预期：两个测试均为 `OK`。

- [x] **步骤 7：运行完整契约测试**

运行：

```bash
python3 -m unittest discover -s tests -v
```

预期：全部测试通过，无 `FAIL` 或 `ERROR`。

- [x] **步骤 8：运行固件构建验证**

运行：

```bash
idf.py build
```

预期：命令退出码为 0，固件链接成功。

- [x] **步骤 9：检查最终差异**

运行：

```bash
git diff --check
git diff -- components/dashboard_ui/dashboard_ui.c components/app_model/live_provider.c tests/test_dashboard_ui_contract.py tests/test_monitor_migration_contract.py
```

预期：`git diff --check` 无输出；相关差异仅包含刷新契约和实现所需的局部修改，并保留文件中原有未提交改动。

由于四个实现文件在开始本任务前已有未提交修改，本计划不创建包含这些文件的提交，避免把用户的既有改动一并提交。
