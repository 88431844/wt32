from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
APP_MODEL = (ROOT / "components/app_model/include/app_model.h").read_text(encoding="utf-8")
MAIN = (ROOT / "main/app_main.c").read_text(encoding="utf-8")
UI = (ROOT / "components/dashboard_ui/dashboard_ui.c").read_text(encoding="utf-8")
PROVIDER = (ROOT / "components/app_model/live_provider.c")
NETWORK = ROOT / "components/network_manager/network_manager.c"
NETWORK_HEADER = ROOT / "components/network_manager/include/network_manager.h"
PORTAL = ROOT / "components/network_manager/provisioning_portal.c"
SETTINGS = ROOT / "components/network_manager/device_settings.c"
SETTINGS_HEADER = ROOT / "components/network_manager/include/device_settings.h"


class MonitorMigrationContractTest(unittest.TestCase):
    def test_snapshot_has_bounded_live_pve_and_nas_models(self) -> None:
        self.assertIn("APP_MAX_PVE_GUESTS", APP_MODEL)
        self.assertIn("APP_MAX_NAS_POOLS", APP_MODEL)
        self.assertIn("pve_guest_t pve_guests[APP_MAX_PVE_GUESTS]", APP_MODEL)
        self.assertIn("nas_pool_t nas_pools[APP_MAX_NAS_POOLS]", APP_MODEL)
        self.assertIn("pve_guest_count", APP_MODEL)
        self.assertIn("nas_pool_count", APP_MODEL)
        self.assertIn("pve_last_error", APP_MODEL)
        self.assertIn("nas_last_error", APP_MODEL)
        self.assertIn("pve_stale", APP_MODEL)
        self.assertIn("nas_stale", APP_MODEL)
        self.assertIn("pve_last_success_ms", APP_MODEL)
        self.assertIn("nas_last_success_ms", APP_MODEL)
        self.assertIn("float pve_load[3]", APP_MODEL)

    def test_snapshot_and_control_api_support_active_page_optional_metrics(self) -> None:
        self.assertIn("APP_MONITOR_NONE", APP_MODEL)
        self.assertIn("APP_MONITOR_NAS", APP_MODEL)
        self.assertIn("APP_MONITOR_PVE", APP_MODEL)
        self.assertIn("app_model_set_active_monitor", APP_MODEL)
        self.assertIn("app_model_set_refresh_seconds", APP_MODEL)
        self.assertIn("char ipv4_address[16]", APP_MODEL)
        self.assertIn("uint64_t nas_rx_bytes_per_second", APP_MODEL)
        self.assertIn("uint64_t nas_tx_bytes_per_second", APP_MODEL)
        self.assertIn("bool nas_network_rate_valid", APP_MODEL)
        self.assertIn("bool nas_cpu_valid", APP_MODEL)
        self.assertIn("bool nas_memory_valid", APP_MODEL)
        self.assertIn("bool nas_temperature_valid", APP_MODEL)

    def test_snapshot_has_bounded_nas_disk_and_uptime_models(self) -> None:
        self.assertIn("#define APP_MAX_NAS_DISKS 8", APP_MODEL)
        self.assertIn("bool healthy", APP_MODEL)
        self.assertIn("char id[APP_TEXT_SMALL]", APP_MODEL)
        self.assertIn("char model[APP_TEXT_MEDIUM]", APP_MODEL)
        self.assertIn("bool temperature_valid", APP_MODEL)
        self.assertIn("bool capacity_valid", APP_MODEL)
        self.assertIn("nas_disk_t nas_disks[APP_MAX_NAS_DISKS]", APP_MODEL)
        self.assertIn("uint32_t nas_disk_count", APP_MODEL)
        self.assertIn("uint32_t nas_uptime_seconds", APP_MODEL)
        self.assertIn("bool nas_uptime_valid", APP_MODEL)

    def test_production_startup_uses_live_provider(self) -> None:
        self.assertTrue(PROVIDER.exists(), "live provider source must exist")
        self.assertIn("app_model_start_live_provider", MAIN)
        self.assertNotIn("app_model_start_mock_provider", MAIN)

    def test_live_provider_has_separate_https_snmp_task_and_refresh_period(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("esp_tls_conn_new_sync", source)
        self.assertIn("esp_tls_cfg_t", source)
        self.assertIn("Authorization", source)
        self.assertIn("SNMP", source)
        self.assertIn("inet_pton", source)
        self.assertIn("network_manager_wait_for_settings_change", source)
        self.assertIn("device_settings_get_string", source)
        self.assertNotIn("skip_cert_common_name_check = true", source)

    def test_ui_is_pve_nas_only_and_has_both_pagination_directions(self) -> None:
        self.assertIn("#define PAGE_COUNT 3", UI)
        self.assertIn('"PVE"', UI)
        self.assertIn('"NAS"', UI)
        self.assertIn("PVE_SUBNAV_LEFT", UI)
        self.assertIn("PVE_SUBNAV_RIGHT", UI)
        self.assertIn("PVE_VM_SCROLL_UP", UI)
        self.assertIn("PVE_VM_SCROLL_DOWN", UI)
        self.assertIn("lv_bar_create", UI)
        self.assertIn("COLOR_RED", UI)
        self.assertIn("COLOR_GREEN", UI)
        self.assertNotIn("create_weather_page", UI)
        self.assertNotIn("create_market_page", UI)

    def test_monitor_settings_use_valid_nvs_keys(self) -> None:
        self.assertTrue(SETTINGS.exists(), "checked settings source must exist")
        self.assertTrue(SETTINGS_HEADER.exists(), "settings API must exist")
        settings = SETTINGS.read_text(encoding="utf-8")
        header = SETTINGS_HEADER.read_text(encoding="utf-8")
        self.assertIn('DEVICE_KEY_PVE_SECRET "pve_secret"', header)
        self.assertIn('DEVICE_KEY_THEME "theme_id"', header)
        self.assertIn('DEVICE_KEY_REFRESH "refresh_s"', header)
        self.assertNotIn('nvs_set_str(handle, "pve_token_secret"', settings)
        self.assertIn("device_settings_set_string_if_present", settings)

    def test_pve_portal_is_on_demand_wpa2_and_bounded(self) -> None:
        self.assertTrue(PORTAL.exists(), "on-demand portal source must exist")
        portal = PORTAL.read_text(encoding="utf-8")
        self.assertIn("WIFI_AUTH_WPA2_PSK", portal)
        self.assertIn("PROVISIONING_TIMEOUT_MS", portal)
        self.assertIn("esp_fill_random", portal)
        self.assertIn("network_manager_stop_pve_portal", portal)
        self.assertNotIn("pve_secret", portal.split("ESP_LOG", 1)[-1])

    def test_network_manager_exposes_nonblocking_settings_apis(self) -> None:
        header = NETWORK_HEADER.read_text(encoding="utf-8")
        self.assertIn("NETWORK_MAX_SCAN_RESULTS", header)
        self.assertIn("network_manager_start_scan", header)
        self.assertIn("network_manager_get_scan_results", header)
        self.assertIn("network_manager_configure_wifi", header)
        self.assertIn("network_manager_start_pve_portal", header)
        self.assertIn("network_manager_wait_for_settings_change", header)

    def test_live_provider_uses_immediate_refresh_and_strict_tls(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("network_manager_wait_for_settings_change", source)
        self.assertIn(".common_name", source)
        self.assertIn(".skip_common_name = false", source)
        self.assertNotIn("skip_cert_common_name_check = true", source)

    def test_pve_tls_supports_exact_leaf_pin_without_disabling_verification(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("pve_pinned_leaf_verify", source)
        self.assertIn("mbedtls_sha256_ret", source)
        self.assertIn("mbedtls_ct_memcmp", source)
        self.assertIn("*flags &= ~MBEDTLS_X509_BADCERT_NOT_TRUSTED", source)
        self.assertIn("MBEDTLS_SSL_VERIFY_OPTIONAL", source)
        self.assertGreaterEqual(source.count("MBEDTLS_ERR_X509_FATAL_ERROR"), 2)
        self.assertNotIn("MBEDTLS_SSL_VERIFY_NONE", source)

    def test_snmp_uses_get_and_getnext_and_validates_responses(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("get_next ? 0xA1 : 0xA0", source)
        self.assertIn("response_request_id != expected_request_id", source)
        self.assertIn("error_status != 0", source)
        self.assertNotIn("get_next ? 0xA1 : 0xA5", source)

    def test_snmp_collection_has_one_deadline_and_uses_only_remaining_time(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        budget = re.search(r"#define SNMP_COLLECT_BUDGET_MS\s+(\d+)", source)
        self.assertIsNotNone(budget)
        self.assertGreaterEqual(int(budget.group(1)), 1800)
        self.assertLessEqual(int(budget.group(1)), 2500)
        self.assertIn("s_snmp_deadline_us", source)
        self.assertIn("snmp_remaining_ms", source)
        self.assertIn("remaining_ms > SNMP_TIMEOUT_MS ? SNMP_TIMEOUT_MS : remaining_ms", source)
        self.assertIn("if (snmp_remaining_ms() == 0) return false;", source)
        self.assertIn(
            "s_snmp_deadline_us = esp_timer_get_time() + SNMP_COLLECT_BUDGET_MS * 1000LL;",
            source,
        )
        self.assertIn("s_snmp_deadline_us = 0;", source)
        self.assertIn(".tv_sec = timeout_ms / 1000", source)
        self.assertIn(".tv_usec = (timeout_ms % 1000) * 1000", source)

    def test_nas_collection_tracks_each_metric_validity_and_any_valid_response(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        for field in ("nas_cpu_valid", "nas_memory_valid", "nas_temperature_valid"):
            self.assertIn(f"snapshot->{field} = false;", source)
        self.assertIn("memory_total > 0 && memory_available <= memory_total", source)
        self.assertIn("snapshot->nas_memory_valid = memory_ok &&", source)
        self.assertIn("snapshot->nas_temperature_valid = temperature_ok && temperature <= 150;", source)
        self.assertIn("snapshot->nas_uptime_valid || snapshot->nas_cpu_valid ||", source)
        self.assertIn("snapshot->nas_memory_valid || snapshot->nas_temperature_valid ||", source)
        self.assertIn("snapshot->nas_pool_count > 0 || snapshot->nas_disk_count > 0", source)

    def test_nas_uptime_prefers_system_uptime_with_agent_fallback(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        system_oid = '"1.3.6.1.2.1.25.1.1.0"'
        agent_oid = '"1.3.6.1.2.1.1.3.0"'
        self.assertIn(system_oid, source)
        self.assertIn(agent_oid, source)
        self.assertLess(source.index(system_oid), source.index(agent_oid))
        self.assertIn("if (!snapshot->nas_uptime_valid)", source)

    def test_nas_memory_excludes_reclaimable_buffers_and_cache(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn('"1.3.6.1.4.1.2021.4.14.0"', source)
        self.assertIn('"1.3.6.1.4.1.2021.4.15.0"', source)
        self.assertIn("memory_reclaimable <= memory_total - memory_available", source)
        self.assertIn(
            "memory_used = memory_total - memory_available - memory_reclaimable",
            source,
        )
        self.assertIn("memory_used = memory_total - memory_available", source)

    def test_nas_cpu_falls_back_when_idle_is_missing_or_out_of_range(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("const bool idle_ok = snmp_get_number", source)
        self.assertIn("if (idle_ok && idle <= 100)", source)
        self.assertIn("const bool user_ok = snmp_get_number", source)
        self.assertIn("const bool system_ok = snmp_get_number", source)
        self.assertIn("const bool fallback_ok = user_ok && system_ok &&", source)
        self.assertIn(
            "user <= 100 && system <= 100 && user + system <= 100",
            source,
        )
        self.assertIn("snapshot->nas_cpu_valid = fallback_ok;", source)
        self.assertNotIn("const bool cpu_ok =", source)

    def test_snmp_uses_numeric_ipv4_without_blocking_dns(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("struct sockaddr_in destination", source)
        self.assertIn("inet_pton(AF_INET, host, &destination.sin_addr)", source)
        self.assertIn("destination.sin_port = htons(161);", source)
        self.assertIn('"NAS IP 无效"', source)
        self.assertNotIn("getaddrinfo", source)
        self.assertNotIn("freeaddrinfo", source)

    def test_provider_collects_guest_ipv4_and_counter64_network_rates(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("network-get-interfaces", source)
        self.assertIn("/interfaces", source)
        self.assertIn("select_guest_ipv4", source)
        self.assertIn("1.3.6.1.2.1.31.1.1.1.6", source)
        self.assertIn("1.3.6.1.2.1.31.1.1.1.10", source)
        self.assertIn("nas_network_rate_valid", source)
        self.assertNotIn("pve_cpu_temperature", source)

    def test_provider_collects_synology_disks_and_nas_uptime(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn('"1.3.6.1.2.1.1.3.0"', source)
        self.assertIn('"1.3.6.1.4.1.6574.2.1.1.2"', source)
        self.assertIn('"1.3.6.1.4.1.6574.2.1.1.3"', source)
        self.assertIn('"1.3.6.1.4.1.6574.2.1.1.5"', source)
        self.assertIn('"1.3.6.1.4.1.6574.2.1.1.6"', source)
        self.assertIn("collect_nas_disks", source)
        self.assertIn("ticks / 100ULL", source)
        self.assertIn("pool->healthy = raid_status == 1", source)
        self.assertIn("disk->capacity_valid = false", source)

    def test_nas_success_log_counts_model_and_temperature_capabilities(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("uint32_t model_count = 0;", source)
        self.assertIn("uint32_t temperature_count = 0;", source)
        self.assertIn("i < snapshot.nas_disk_count", source)
        self.assertIn("snapshot.nas_disks[i].model[0] != '\\0'", source)
        self.assertIn("snapshot.nas_disks[i].temperature_valid", source)
        self.assertIn('" models=%" PRIu32 " temperatures=%" PRIu32', source)

    def test_provider_schedules_only_the_selected_monitor(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("s_control_queue", source)
        self.assertIn("app_model_set_active_monitor", source)
        self.assertIn("app_model_set_refresh_seconds", source)
        self.assertIn("control.active_monitor == APP_MONITOR_PVE", source)
        self.assertIn("control.active_monitor == APP_MONITOR_NAS", source)
        self.assertIn("xQueueOverwrite(s_control_queue", source)

    def test_provider_supports_same_refresh_intervals_and_defaults_to_five_seconds(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("#define DEFAULT_REFRESH_SECONDS 5", source)
        self.assertIn(
            "return seconds == 5 || seconds == 10 || seconds == 30 || seconds == 60;",
            source,
        )
        self.assertNotIn("seconds == 120", source)
        self.assertIn("elapsed < interval ? interval - elapsed : 0", source)

    def test_provider_preserves_each_last_successful_section(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("candidate = snapshot", source)
        self.assertIn("candidate.pve_stale = true", source)
        self.assertIn("candidate.nas_stale = true", source)

    def test_live_provider_keeps_large_workspace_off_task_stack(self) -> None:
        source = PROVIDER.read_text(encoding="utf-8")
        self.assertIn("static app_snapshot_t snapshot;", source)
        self.assertIn("static app_snapshot_t candidate;", source)
        self.assertIn("static char pve_host[32]", source)
        self.assertIn("pve_ca[4096]", source)
        self.assertIn("static char nas_host[32]", source)
        self.assertNotIn("app_snapshot_t snapshot = {0};", source)
        self.assertNotIn("app_snapshot_t candidate = snapshot", source)


if __name__ == "__main__":
    unittest.main()
