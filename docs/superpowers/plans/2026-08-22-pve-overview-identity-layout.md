# PVE Overview Identity Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the combined PVE identity sentence with three separated horizontal fields for name, version, and IP, while using only the status dot to communicate online state.

**Architecture:** Keep all changes inside the existing LVGL dashboard component. Store three label pointers and two separator pointers in the UI state, update their content from the current snapshot, and clear secondary fields for loading or offline messages without changing provider data or navigation.

**Tech Stack:** ESP-IDF 4.4.8, C11, LVGL 8.3.11, Python `unittest` source-contract tests, CMake/Ninja.

---

## File Structure

- `tests/test_dashboard_ui_contract.py`: guard the three-field geometry, separators, state updates, theme behavior, and removal of the combined online sentence.
- `components/dashboard_ui/dashboard_ui.c`: create, theme, and update the PVE identity labels and separators.

### Task 1: Add The PVE Identity Row Contract

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`

- [ ] **Step 1: Write the failing UI contract**

Add this test to `DashboardUiContractTest`:

```python
def test_pve_identity_row_uses_three_fields_and_two_theme_aware_separators(self):
    for field in ("pve_name", "pve_version", "pve_host",
                  "pve_identity_dividers[2]"):
        self.assertIn(field, UI)

    for marker in (
        'make_label(summary, "name --", 24, 8, 132',
        'make_rule(summary, 160, 7, 1, 20)',
        'make_label(summary, "version --", 170, 8, 132',
        'make_rule(summary, 306, 7, 1, 20)',
        'make_label(summary, "IP --", 316, 8, 132',
    ):
        self.assertIn(marker, UI)

    update = UI[UI.index("static void update_pve(void)") :]
    update = update[: update.index("static void update_nas(void)")]
    for marker in (
        'lv_label_set_text_fmt(s_ui.pve_name, "name %s"',
        'lv_label_set_text_fmt(s_ui.pve_version, "version %s"',
        'lv_label_set_text_fmt(s_ui.pve_host, "IP %s"',
    ):
        self.assertIn(marker, update)
    self.assertNotIn(" 在线", update)
    self.assertNotIn("pve name:%s version:%s", update)

    message = UI[UI.index("static void show_monitor_message") :]
    message = message[: message.index("void dashboard_ui_update")]
    self.assertIn("lv_label_set_text(s_ui.pve_name, message)", message)
    self.assertIn('lv_label_set_text(s_ui.pve_version, "--")', message)
    self.assertIn('lv_label_set_text(s_ui.pve_host, "--")', message)

    theme = UI[UI.index("static void apply_theme(void)") :]
    theme = theme[: theme.index("static void page_event")]
    self.assertIn("s_ui.pve_identity_dividers", theme)
    self.assertIn("color(COLOR_LINE)", theme)
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
python3 -m unittest \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_pve_identity_row_uses_three_fields_and_two_theme_aware_separators -v
```

Expected: FAIL because `pve_name`, `pve_version`, `pve_host`, the divider array,
and the three-field creation/update markers do not exist.

### Task 2: Implement The Three-Field LVGL Row

**Files:**
- Modify: `components/dashboard_ui/dashboard_ui.c:116-145`
- Modify: `components/dashboard_ui/dashboard_ui.c:531-552`
- Modify: `components/dashboard_ui/dashboard_ui.c:726-739`
- Modify: `components/dashboard_ui/dashboard_ui.c:1533-1550`
- Modify: `components/dashboard_ui/dashboard_ui.c:1758-1773`

- [ ] **Step 1: Replace the combined identity pointer with explicit row objects**

Replace `lv_obj_t *pve_identity;` in the UI state with:

```c
lv_obj_t *pve_name;
lv_obj_t *pve_version;
lv_obj_t *pve_host;
lv_obj_t *pve_identity_dividers[2];
```

- [ ] **Step 2: Create the three labels and two separators at fixed positions**

Replace the combined identity label in `create_pve_page()` with:

```c
s_ui.pve_name = make_label(summary, "name --", 24, 8, 132,
                           &app_font_14, COLOR_TEXT);
s_ui.pve_identity_dividers[0] = make_rule(summary, 160, 7, 1, 20);
s_ui.pve_version = make_label(summary, "version --", 170, 8, 132,
                              &app_font_14, COLOR_TEXT);
s_ui.pve_identity_dividers[1] = make_rule(summary, 306, 7, 1, 20);
s_ui.pve_host = make_label(summary, "IP --", 316, 8, 132,
                           &app_font_14, COLOR_TEXT);
```

These coordinates leave the existing status dot at `x=10`, keep all three
fields at exactly 132 pixels on the current `y=8` baseline, and stop at `x=448`
inside the 464-pixel surface.

- [ ] **Step 3: Keep separator colors synchronized with the selected theme**

Add this block inside `apply_theme()` after the shared style colors are updated:

```c
for (size_t i = 0; i < 2; ++i) {
    if (s_ui.pve_identity_dividers[i] != NULL) {
        lv_obj_set_style_bg_color(s_ui.pve_identity_dividers[i],
                                  color(COLOR_LINE), 0);
    }
}
```

- [ ] **Step 4: Update online and offline snapshot rendering**

Replace the identity formatting block at the start of `update_pve()` with:

```c
if (snapshot->pve_online) {
    lv_label_set_text_fmt(s_ui.pve_name, "name %s",
                          snapshot->pve_name[0] ? snapshot->pve_name : "p330");
    lv_label_set_text_fmt(s_ui.pve_version, "version %s",
                          snapshot->pve_version[0] ? snapshot->pve_version : "--");
    lv_label_set_text_fmt(s_ui.pve_host, "IP %s",
                          snapshot->pve_host[0] ? snapshot->pve_host : "--");
    lv_obj_set_style_bg_color(s_ui.pve_status_dot, color(COLOR_GREEN), 0);
} else {
    snprintf(buffer, sizeof(buffer), "PVE %s",
             snapshot->pve_configured ? "离线" : "未配置");
    lv_label_set_text(s_ui.pve_name, buffer);
    lv_label_set_text(s_ui.pve_version, "--");
    lv_label_set_text(s_ui.pve_host, "--");
    lv_obj_set_style_bg_color(s_ui.pve_status_dot, color(COLOR_GRAY), 0);
}
```

Do not apply green text color to any identity label. The green dot is the only
online indicator, and the string `在线` is no longer rendered in this row.

- [ ] **Step 5: Clear secondary fields for loading and offline model events**

Replace the PVE branch in `show_monitor_message()` with:

```c
} else if (monitor == APP_MONITOR_PVE) {
    lv_label_set_text(s_ui.pve_name, message);
    lv_label_set_text(s_ui.pve_version, "--");
    lv_label_set_text(s_ui.pve_host, "--");
    lv_obj_set_style_bg_color(s_ui.pve_status_dot, color(COLOR_GRAY), 0);
    for (int i = 0; i < VM_VISIBLE; ++i) set_hidden(s_ui.vm_rows[i], true);
}
```

- [ ] **Step 6: Run the focused contract and verify GREEN**

Run:

```bash
python3 -m unittest \
  tests.test_dashboard_ui_contract.DashboardUiContractTest.test_pve_identity_row_uses_three_fields_and_two_theme_aware_separators -v
```

Expected: PASS.

- [ ] **Step 7: Run all Dashboard UI contracts**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract -v
```

Expected: all Dashboard UI contract tests pass.

### Task 3: Verify And Commit The Firmware Change

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py`
- Modify: `components/dashboard_ui/dashboard_ui.c`

- [ ] **Step 1: Run the complete repository test suite**

Run:

```bash
python3 -m unittest discover -s tests -p 'test_*.py'
```

Expected: all tests pass with zero failures or errors.

- [ ] **Step 2: Build and size-check with ESP-IDF 4.4.8**

Run:

```bash
source /Users/luckmiracle/esp/esp-idf/export.sh
idf.py --version
idf.py build
idf.py size
```

Expected: ESP-IDF reports `v4.4.8`, the build completes, and
`wt32_dashboard.bin` remains below the `0x1d0000` application partition.

- [ ] **Step 3: Review the final diff and repository state**

Run:

```bash
git diff --check
git diff -- components/dashboard_ui/dashboard_ui.c tests/test_dashboard_ui_contract.py
git status --short --branch
```

Expected: only the two implementation files are modified, plus the pre-existing
untracked `openhasp/` directory. Do not stage `openhasp/`.

- [ ] **Step 4: Commit the implementation**

Run:

```bash
git add components/dashboard_ui/dashboard_ui.c tests/test_dashboard_ui_contract.py
git commit -m "feat(ui): separate PVE identity fields"
```

Expected: one implementation commit on `wt32-Nas-pve-monitor` containing only
the two intended files.
