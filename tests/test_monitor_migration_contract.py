from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODEL = (ROOT / "components/app_model/include/app_snapshot.h").read_text(encoding="utf-8")
PROVIDER = (ROOT / "components/app_model/live_provider.c").read_text(encoding="utf-8")
MAIN = (ROOT / "main/app_main.c").read_text(encoding="utf-8")
UI = (ROOT / "components/dashboard_ui/dashboard_ui.c").read_text(encoding="utf-8")
SETTINGS_HEADER = (ROOT / "components/network_manager/include/device_settings.h").read_text(encoding="utf-8")

class MonitorMigrationContractTest(unittest.TestCase):
    def test_snapshot_collections_are_dynamic_and_uncapped(self):
        for declaration in ("size_t pve_guest_count", "pve_guest_t *pve_guests",
                            "size_t nas_pool_count", "nas_pool_t *nas_pools",
                            "size_t nas_disk_count", "nas_disk_t *nas_disks"):
            self.assertIn(declaration, MODEL)
        self.assertNotIn("APP_MAX_PVE_GUESTS", MODEL + PROVIDER + UI)
        self.assertNotIn("APP_MAX_NAS_POOLS", MODEL + PROVIDER + UI)
        self.assertNotIn("APP_MAX_NAS_DISKS", MODEL + PROVIDER + UI)

    def test_snapshot_lifecycle_has_ownership_operations(self):
        for name in ("app_snapshot_create", "app_snapshot_destroy", "app_snapshot_reserve_pve_guests",
                     "app_snapshot_reserve_nas_pools", "app_snapshot_reserve_nas_disks",
                     "app_snapshot_clone", "app_snapshot_move", "app_snapshot_dispose"):
            self.assertIn(name, MODEL)

    def test_provider_queue_uses_typed_events_and_frees_dropped_snapshots(self):
        self.assertIn("sizeof(app_model_event_t)", PROVIDER)
        self.assertIn("app_snapshot_destroy(dropped.snapshot)", PROVIDER)
        for event in ("APP_MODEL_EVENT_LOADING", "APP_MODEL_EVENT_SNAPSHOT",
                      "APP_MODEL_EVENT_REFRESH_FAILED", "APP_MODEL_EVENT_OFFLINE"):
            self.assertIn(event, PROVIDER)
        self.assertNotIn("xQueueOverwrite(s_snapshot_queue", PROVIDER)

    def test_main_transfers_then_releases_event_snapshot(self):
        self.assertIn("app_model_event_t event", MAIN)
        self.assertIn("dashboard_ui_update(&event)", MAIN)
        self.assertIn("app_snapshot_destroy(event.snapshot)", MAIN)

    def test_pve_response_and_guest_collection_are_dynamic(self):
        self.assertNotIn("HTTP_RESPONSE_MAX", PROVIDER)
        for marker in ("grow_http_buffer", "cJSON_GetArraySize(data)",
                       "app_snapshot_reserve_pve_guests", "response_complete"):
            self.assertIn(marker, PROVIDER)
        self.assertIn("MBEDTLS_ERR_SSL_WANT_READ", PROVIDER)
        self.assertIn("read_deadline_us", PROVIDER)

    def test_pve_node_status_collects_uptime_and_cpu_model(self):
        for declaration in (
            "uint32_t pve_uptime_seconds;",
            "char pve_cpu_model[APP_TEXT_LARGE];",
        ):
            self.assertIn(declaration, MODEL)

        parser = PROVIDER[
            PROVIDER.index("static void parse_pve_node"):
            PROVIDER.index("static bool parse_pve_guests")
        ]
        for marker in (
            'json_u64(data, "uptime")',
            'json_string(cpuinfo, "model")',
            "snapshot->pve_uptime_seconds",
            "snapshot->pve_cpu_model",
        ):
            self.assertIn(marker, parser)
        self.assertNotIn("temperature", parser.lower())

    def test_pve_guest_network_interfaces_are_requested_with_get(self):
        guest_network_collection = PROVIDER[
            PROVIDER.index('"/api2/json/nodes/%s/qemu/%" PRIu32 "/agent/network-get-interfaces"'):
        ]
        guest_network_collection = guest_network_collection[
            :guest_network_collection.index("snapshot->pve_online = true;")
        ]
        self.assertIn("path, false, &agent_root", guest_network_collection)
        self.assertNotIn("path, true, &agent_root", guest_network_collection)

    def test_snmp_walks_complete_dynamic_subtrees(self):
        self.assertNotIn("SNMP_COLLECT_BUDGET_MS", PROVIDER)
        self.assertNotIn("SNMP_MAX_POOLS", PROVIDER)
        for marker in ("snmp_oid_compare", "app_snapshot_reserve_nas_pools",
                       "app_snapshot_reserve_nas_disks",
                       "snmp_oid_compare(value.oid, cursor) <= 0"):
            self.assertIn(marker, PROVIDER)

    def test_provider_keeps_per_exchange_timeout_and_strict_tls(self):
        self.assertIn("SNMP_TIMEOUT_MS", PROVIDER)
        self.assertIn(".skip_common_name = false", PROVIDER)
        self.assertIn("pve_pinned_leaf_verify", PROVIDER)
        self.assertIn("mbedtls_ct_memcmp", PROVIDER)
        self.assertNotIn("MBEDTLS_SSL_VERIFY_NONE", PROVIDER)

    def test_provider_publishes_only_complete_candidates(self):
        self.assertIn("app_snapshot_clone(candidate, &snapshot)", PROVIDER)
        self.assertIn("app_snapshot_move(&snapshot, candidate)", PROVIDER)
        self.assertIn("publish_snapshot_clone(&snapshot", PROVIDER)
        self.assertNotIn("candidate = snapshot", PROVIDER)

    def test_first_failure_waits_for_timeout_and_later_failure_keeps_cache(self):
        self.assertIn("#define INITIAL_REQUEST_TIMEOUT_MS 5000", PROVIDER)
        self.assertIn("publish_collection_failure", PROVIDER)
        self.assertIn("if (elapsed < timeout) vTaskDelay(timeout - elapsed);", PROVIDER)
        self.assertIn("APP_MODEL_EVENT_REFRESH_FAILED", PROVIDER)
        self.assertIn("APP_MODEL_EVENT_OFFLINE", PROVIDER)

    def test_startup_prefetches_other_monitor_once_then_polls_active_page(self):
        for marker in (
            "startup_primary_monitor",
            "startup_prefetch_monitor",
            "startup_primary_complete",
            "startup_prefetch_complete",
            "cycle_monitor",
            "cycle_monitor = control.active_monitor",
            "cycle_monitor == APP_MONITOR_PVE",
            "cycle_monitor == APP_MONITOR_NAS",
            "DEVICE_KEY_HOME_PAGE",
            "saved_homepage == 1 ? APP_MONITOR_PVE : APP_MONITOR_NAS",
            "Startup primary refresh finished",
            "Startup prefetch finished",
        ):
            self.assertIn(marker, PROVIDER)
        self.assertIn("snapshot->pve_last_success_ms > 0", UI)
        self.assertIn("snapshot->nas_last_success_ms > 0", UI)

    def test_homepage_key_is_valid_short_nvs_key(self):
        self.assertIn('#define DEVICE_KEY_HOME_PAGE "home_page"', SETTINGS_HEADER)

if __name__ == "__main__":
    unittest.main()
