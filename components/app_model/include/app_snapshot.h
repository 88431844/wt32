#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_TEXT_SMALL 32
#define APP_TEXT_MEDIUM 64
#define APP_TEXT_LARGE 128

typedef enum {
    APP_MONITOR_NONE = 0,
    APP_MONITOR_NAS = 1,
    APP_MONITOR_PVE = 2,
} app_monitor_t;

typedef struct {
    uint32_t vmid;
    char name[APP_TEXT_MEDIUM];
    char kind[8];
    char ipv4_address[16];
    bool running;
    float cpu_percent;
    uint64_t memory_used;
    uint64_t memory_total;
    uint64_t disk_used;
    uint64_t disk_total;
    uint32_t cpu_cores;
    uint32_t uptime_seconds;
    bool guest_agent;
} pve_guest_t;

typedef struct {
    char name[APP_TEXT_MEDIUM];
    char description[APP_TEXT_LARGE];
    char filesystem[APP_TEXT_SMALL];
    char raid_type[APP_TEXT_SMALL];
    uint64_t used_bytes;
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool healthy;
} nas_pool_t;

typedef struct {
    char id[APP_TEXT_SMALL];
    char model[APP_TEXT_MEDIUM];
    bool healthy;
    int temperature_c;
    bool temperature_valid;
    uint64_t capacity_bytes;
    bool capacity_valid;
} nas_disk_t;

typedef struct {
    uint32_t revision;
    uint32_t uptime_seconds;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool wifi_connected;
    char ip_address[16];

    bool pve_online;
    bool pve_configured;
    char pve_name[APP_TEXT_MEDIUM];
    char pve_version[APP_TEXT_SMALL];
    char pve_host[32];
    float pve_cpu_percent;
    uint64_t pve_memory_used;
    uint64_t pve_memory_total;
    uint64_t pve_storage_used;
    uint64_t pve_storage_total;
    float pve_load[3];
    uint32_t pve_cpu_cores;
    size_t pve_guest_count;
    size_t pve_guest_capacity;
    uint32_t pve_running_count;
    pve_guest_t *pve_guests;
    char pve_last_error[APP_TEXT_LARGE];
    bool pve_stale;
    uint32_t pve_last_success_ms;

    bool nas_online;
    bool nas_configured;
    char nas_host[32];
    char nas_model[APP_TEXT_MEDIUM];
    float nas_cpu_percent;
    bool nas_cpu_valid;
    float nas_memory_percent;
    bool nas_memory_valid;
    int nas_temperature;
    bool nas_temperature_valid;
    uint64_t nas_rx_bytes_per_second;
    uint64_t nas_tx_bytes_per_second;
    bool nas_network_rate_valid;
    uint32_t nas_uptime_seconds;
    bool nas_uptime_valid;
    size_t nas_pool_count;
    size_t nas_pool_capacity;
    nas_pool_t *nas_pools;
    size_t nas_disk_count;
    size_t nas_disk_capacity;
    nas_disk_t *nas_disks;
    char nas_last_error[APP_TEXT_LARGE];
    bool nas_stale;
    uint32_t nas_last_success_ms;
} app_snapshot_t;

typedef enum {
    APP_MODEL_EVENT_LOADING = 0,
    APP_MODEL_EVENT_SNAPSHOT,
    APP_MODEL_EVENT_REFRESH_FAILED,
    APP_MODEL_EVENT_OFFLINE,
} app_model_event_kind_t;

typedef struct {
    app_model_event_kind_t kind;
    app_monitor_t monitor;
    app_snapshot_t *snapshot;
    char error[APP_TEXT_LARGE];
} app_model_event_t;

void app_snapshot_init(app_snapshot_t *snapshot);
void app_snapshot_dispose(app_snapshot_t *snapshot);
app_snapshot_t *app_snapshot_create(void);
void app_snapshot_destroy(app_snapshot_t *snapshot);
bool app_snapshot_reserve_pve_guests(app_snapshot_t *snapshot, size_t capacity);
bool app_snapshot_reserve_nas_pools(app_snapshot_t *snapshot, size_t capacity);
bool app_snapshot_reserve_nas_disks(app_snapshot_t *snapshot, size_t capacity);
bool app_snapshot_clone(app_snapshot_t *destination, const app_snapshot_t *source);
void app_snapshot_move(app_snapshot_t *destination, app_snapshot_t *source);

#ifdef __cplusplus
}
#endif
