# PVE Running-First Guest Sort Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sort PVE guests so running VMs appear first and each status group is ordered by ascending VMID.

**Architecture:** Add a dependency-free in-place guest sorting module to `app_model`, matching the repository's native-test pattern for NAS sorting. Call it once after the complete PVE guest array is parsed and before per-guest network lookups, so every snapshot consumer sees one deterministic order.

**Tech Stack:** C11, ESP-IDF 4.4.8, Python `unittest`, host `cc`

---

### Task 1: Implement And Test PVE Guest Ordering

**Files:**
- Create: `components/app_model/include/pve_guest_sort.h`
- Create: `components/app_model/pve_guest_sort.c`
- Create: `tests/native/pve_guest_sort_test.c`
- Create: `tests/test_pve_guest_sort_native.py`
- Modify: `components/app_model/CMakeLists.txt:1`

- [ ] **Step 1: Write the failing native sorting test**

Create `tests/native/pve_guest_sort_test.c`:

```c
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pve_guest_sort.h"

static pve_guest_t guest(uint32_t vmid, bool running)
{
    pve_guest_t value = {0};
    value.vmid = vmid;
    value.running = running;
    return value;
}

static void test_running_guests_precede_stopped_guests(void)
{
    pve_guest_t guests[] = {
        guest(102, false), guest(105, true), guest(100, true),
        guest(103, false), guest(101, true),
    };
    pve_guest_sort(guests, 5);
    const uint32_t expected[] = {100, 101, 105, 102, 103};
    for (size_t i = 0; i < 5; ++i) assert(guests[i].vmid == expected[i]);
    for (size_t i = 0; i < 3; ++i) assert(guests[i].running);
    for (size_t i = 3; i < 5; ++i) assert(!guests[i].running);
}

static void test_same_status_guests_use_ascending_vmid(void)
{
    pve_guest_t guests[] = {
        guest(300, false), guest(100, false), guest(200, false),
    };
    pve_guest_sort(guests, 3);
    assert(guests[0].vmid == 100);
    assert(guests[1].vmid == 200);
    assert(guests[2].vmid == 300);
}

static void test_small_collections_are_accepted(void)
{
    pve_guest_t one[] = {guest(100, true)};
    pve_guest_sort(NULL, 0);
    pve_guest_sort(one, 1);
    assert(one[0].vmid == 100 && one[0].running);
}

int main(void)
{
    test_running_guests_precede_stopped_guests();
    test_same_status_guests_use_ascending_vmid();
    test_small_collections_are_accepted();
    puts("PVE guest sort tests passed");
    return 0;
}
```

Create `tests/test_pve_guest_sort_native.py`:

```python
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class PveGuestSortNativeTest(unittest.TestCase):
    def test_running_first_guest_sorting(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "pve_guest_sort_test"
            compile_result = subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "components/app_model/include"),
                    str(ROOT / "tests/native/pve_guest_sort_test.c"),
                    str(ROOT / "components/app_model/pve_guest_sort.c"),
                    "-o", str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run(
                [str(executable)], capture_output=True, text=True
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)
            self.assertIn("PVE guest sort tests passed", run_result.stdout)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
python3 -m unittest tests.test_pve_guest_sort_native -v
```

Expected: FAIL because `pve_guest_sort.h` and `pve_guest_sort.c` do not exist.

- [ ] **Step 3: Implement the minimal sorting module**

Create `components/app_model/include/pve_guest_sort.h`:

```c
#pragma once

#include <stddef.h>

#include "app_snapshot.h"

void pve_guest_sort(pve_guest_t *guests, size_t count);
```

Create `components/app_model/pve_guest_sort.c`:

```c
#include "pve_guest_sort.h"

#include <stdlib.h>

static int compare_guests(const void *left_pointer, const void *right_pointer)
{
    const pve_guest_t *left = left_pointer;
    const pve_guest_t *right = right_pointer;
    if (left->running != right->running) return left->running ? -1 : 1;
    if (left->vmid == right->vmid) return 0;
    return left->vmid < right->vmid ? -1 : 1;
}

void pve_guest_sort(pve_guest_t *guests, size_t count)
{
    if (guests == NULL || count < 2) return;
    qsort(guests, count, sizeof(*guests), compare_guests);
}
```

Add the source to `components/app_model/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "live_provider.c" "app_snapshot.c" "pve_guest_sort.c"
```

- [ ] **Step 4: Run the focused test and verify GREEN**

Run:

```bash
python3 -m unittest tests.test_pve_guest_sort_native -v
```

Expected: PASS with `PVE guest sort tests passed`.

- [ ] **Step 5: Commit the tested sorting module**

```bash
git add components/app_model/CMakeLists.txt \
  components/app_model/include/pve_guest_sort.h \
  components/app_model/pve_guest_sort.c \
  tests/native/pve_guest_sort_test.c tests/test_pve_guest_sort_native.py
git commit -m "feat(pve): add running-first guest sorting"
```

### Task 2: Apply Sorting To Parsed PVE Guests

**Files:**
- Modify: `components/app_model/live_provider.c:1`
- Modify: `components/app_model/live_provider.c:466`
- Modify: `tests/test_monitor_migration_contract.py:48`

- [ ] **Step 1: Write the failing provider integration contract**

Add this test to `MonitorMigrationContractTest`:

```python
    def test_pve_guests_are_sorted_before_network_collection(self):
        collection = PROVIDER[
            PROVIDER.index('"/api2/json/cluster/resources?type=vm"'):
            PROVIDER.index("snapshot->pve_online = true;")
        ]
        self.assertIn('#include "pve_guest_sort.h"', PROVIDER)
        self.assertIn(
            "pve_guest_sort(snapshot->pve_guests, snapshot->pve_guest_count);",
            collection,
        )
        self.assertLess(
            collection.index("pve_guest_sort("),
            collection.index("for (size_t i = 0; i < snapshot->pve_guest_count; ++i)"),
        )
```

- [ ] **Step 2: Run the focused contract and verify RED**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_pve_guests_are_sorted_before_network_collection -v
```

Expected: FAIL because `live_provider.c` does not include or call the sorter.

- [ ] **Step 3: Integrate the sorter**

Add the header with the other project includes:

```c
#include "pve_guest_sort.h"
```

After the guest response is parsed and deleted, but before the network lookup
loop, add:

```c
    pve_guest_sort(snapshot->pve_guests, snapshot->pve_guest_count);
```

- [ ] **Step 4: Run the focused contract and verify GREEN**

Run:

```bash
python3 -m unittest tests.test_monitor_migration_contract.MonitorMigrationContractTest.test_pve_guests_are_sorted_before_network_collection -v
```

Expected: PASS.

- [ ] **Step 5: Run the full test suite and firmware build**

```bash
python3 -m unittest discover -s tests -v
source /Users/luckmiracle/esp/esp-idf/export.sh
idf.py build
```

Expected: all Python tests pass and ESP-IDF reports `Project build complete`.

- [ ] **Step 6: Commit the provider integration**

```bash
git add components/app_model/live_provider.c tests/test_monitor_migration_contract.py
git commit -m "fix(pve): list running guests first"
```

### Task 3: Flash And Verify The Device

**Files:**
- No source changes.

- [ ] **Step 1: Reconfigure the committed firmware version and build**

```bash
source /Users/luckmiracle/esp/esp-idf/export.sh
idf.py reconfigure
idf.py build
```

Expected: the app version matches the final implementation commit and the build
completes successfully.

- [ ] **Step 2: Confirm and flash the documented WT32-SC01 target**

```bash
esptool.py --chip esp32 --port /dev/cu.usbserial-01E725F0 chip_id
idf.py -p /dev/cu.usbserial-01E725F0 flash
```

Expected: the MAC is `84:0d:8e:e0:8d:40`, every written image passes hash
verification, and the device hard-resets. Do not erase NVS.

- [ ] **Step 3: Monitor one complete startup refresh**

```bash
idf.py -p /dev/cu.usbserial-01E725F0 monitor
```

Expected: the committed app version boots, the dashboard is created, and PVE
refresh succeeds without a crash or reset loop. Exit with `Ctrl-]`.

