# PVE Virtual Machine vCPU List Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show each PVE guest's configured virtual CPU count in the virtual machine list under a `vcpu` header.

**Architecture:** Keep the existing PVE snapshot and provider mapping unchanged because `maxcpu` is already stored in `pve_guest_t.cpu_cores`. Change only the LVGL list header and row renderer, with a source-level contract test that isolates list creation and `update_pve()` so the detail view remains untouched.

**Tech Stack:** C, LVGL, ESP-IDF, Python `unittest`

---

### Task 1: Render vCPU Count In The PVE Guest List

**Files:**
- Modify: `tests/test_dashboard_ui_contract.py:209`
- Modify: `components/dashboard_ui/dashboard_ui.c:826`
- Modify: `components/dashboard_ui/dashboard_ui.c:1771`

- [ ] **Step 1: Write the failing UI contract test**

In `test_pve_vm_list_has_four_aligned_rows_and_vertical_pager`, keep the
existing geometry assertions, change the expected list header from `CPU` to
`vcpu`, and add focused creation/update assertions:

```python
        for header in ('"虚拟机"', '"IP"', '"vcpu"', '"内存"'):
            self.assertIn(header, UI)
        pve_creation = UI[
            UI.index("static void create_pve_page"):
            UI.index("static void show_nas_pools")
        ]
        self.assertIn(
            's_ui.vm_header_cpu = make_label(s_ui.pve_vm_list, "vcpu", 288, 6, 40,',
            pve_creation,
        )
        self.assertNotIn(
            's_ui.vm_header_cpu = make_label(s_ui.pve_vm_list, "CPU"',
            pve_creation,
        )
        pve_update = UI[
            UI.index("static void update_pve"):
            UI.index("static void update_nas")
        ]
        self.assertIn("if (guest->cpu_cores > 0)", pve_update)
        self.assertIn(
            'lv_label_set_text_fmt(s_ui.vm_row_cpu[i], "%" PRIu32, guest->cpu_cores);',
            pve_update,
        )
        self.assertIn('lv_label_set_text(s_ui.vm_row_cpu[i], "--");', pve_update)
        self.assertNotIn("format_percent(cpu", pve_update)
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract.DashboardUiContractTest.test_pve_vm_list_has_four_aligned_rows_and_vertical_pager -v
```

Expected: FAIL because the list still creates a `CPU` header with width 32 and
formats `guest->cpu_percent`.

- [ ] **Step 3: Implement the minimal list rendering change**

In `create_pve_page()`, retain the existing 48-pixel column boundaries and use
the full available content width for the new header:

```c
    s_ui.vm_header_cpu = make_label(s_ui.pve_vm_list, "vcpu", 288, 6, 40,
                                    &app_font_14, COLOR_MUTED);
```

In the visible-row loop in `update_pve()`, remove the unused CPU percentage
buffer and render the configured count without a suffix:

```c
        char capacity[52];
        if (guest->cpu_cores > 0)
            lv_label_set_text_fmt(s_ui.vm_row_cpu[i], "%" PRIu32, guest->cpu_cores);
        else
            lv_label_set_text(s_ui.vm_row_cpu[i], "--");
```

Do not modify `update_vm_detail()`, `parse_pve_guests()`, list rule positions,
or the IP and memory columns.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run:

```bash
python3 -m unittest tests.test_dashboard_ui_contract.DashboardUiContractTest.test_pve_vm_list_has_four_aligned_rows_and_vertical_pager -v
```

Expected: PASS.

- [ ] **Step 5: Run the full Python test suite**

Run:

```bash
python3 -m unittest discover -s tests -v
```

Expected: all tests PASS with no errors or failures.

- [ ] **Step 6: Build the ESP-IDF firmware**

Run:

```bash
idf.py build
```

Expected: the firmware build completes successfully.

- [ ] **Step 7: Review and commit the implementation**

Run:

```bash
git diff --check
git status --short
git add tests/test_dashboard_ui_contract.py components/dashboard_ui/dashboard_ui.c
git commit -m "fix(pve): show guest vCPU counts in list"
```

Expected: only the two intended implementation files are committed; unrelated
untracked files remain outside the commit.

