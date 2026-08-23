#include "app_model.h"

#include <errno.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "device_settings.h"
#include "esp_tls.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
#include "network_manager.h"

#define DEFAULT_REFRESH_SECONDS 5
#define HTTP_TIMEOUT_MS 5000
#define INITIAL_REQUEST_TIMEOUT_MS 5000
#define HTTP_INITIAL_CAPACITY 4096
#define SNMP_RESPONSE_MAX 2048
#define SNMP_TIMEOUT_MS 1800

static const char *TAG = "live_provider";
static QueueHandle_t s_snapshot_queue;
static QueueHandle_t s_control_queue;

typedef struct {
    app_monitor_t active_monitor;
    uint8_t refresh_seconds;
} provider_control_t;

static bool valid_refresh_seconds(uint8_t seconds)
{
    return seconds == 5 || seconds == 10 || seconds == 30 || seconds == 60;
}

typedef enum {
    SNMP_VALUE_NONE,
    SNMP_VALUE_STRING,
    SNMP_VALUE_INTEGER,
    SNMP_VALUE_UNSIGNED,
} snmp_value_type_t;

typedef struct {
    snmp_value_type_t type;
    uint64_t number;
    char text[APP_TEXT_LARGE];
    uint32_t last_index;
    char oid[96];
} snmp_value_t;

typedef struct {
    unsigned char sha256[32];
    bool ready;
} pve_pin_context_t;

static pve_pin_context_t s_pve_pin;

typedef struct {
    uint32_t interface_index;
    uint64_t rx_octets;
    uint64_t tx_octets;
    int64_t sampled_at_us;
    bool valid;
} nas_rate_baseline_t;

static nas_rate_baseline_t s_nas_rate_baseline;

static void copy_text(char *destination, size_t size, const char *source)
{
    if (destination == NULL || size == 0) return;
    if (source == NULL) source = "";
    snprintf(destination, size, "%s", source);
}

static void load_monitor_settings(char *pve_host, char *pve_node, char *token_id,
                                  char *token_secret, char *pve_ca, char *nas_host,
                                  char *community)
{
    if (device_settings_get_string(DEVICE_KEY_PVE_HOST, pve_host, 32) != ESP_OK)
        copy_text(pve_host, 32, "192.168.31.34");
    if (device_settings_get_string(DEVICE_KEY_PVE_NODE, pve_node, 32) != ESP_OK)
        copy_text(pve_node, 32, "p330");
    if (device_settings_get_string(DEVICE_KEY_PVE_TOKEN_ID, token_id, 64) != ESP_OK)
        copy_text(token_id, 64, "monitor@pve!screen");
    (void)device_settings_get_string(DEVICE_KEY_PVE_SECRET, token_secret, 96);
    (void)device_settings_get_string(DEVICE_KEY_PVE_CA, pve_ca, 4096);
    if (device_settings_get_string(DEVICE_KEY_NAS_HOST, nas_host, 32) != ESP_OK)
        copy_text(nas_host, 32, "192.168.31.105");
    (void)device_settings_get_string(DEVICE_KEY_SNMP_COMMUNITY, community, 64);
}

static int pve_pinned_leaf_verify(void *context, mbedtls_x509_crt *certificate,
                                  int depth, uint32_t *flags)
{
    pve_pin_context_t *pin = context;
    if (pin == NULL || !pin->ready || certificate == NULL ||
        certificate->raw.p == NULL || flags == NULL || depth != 0)
        return MBEDTLS_ERR_X509_FATAL_ERROR;

    unsigned char actual_sha256[32];
    if (mbedtls_sha256_ret(certificate->raw.p, certificate->raw.len,
                           actual_sha256, 0) != 0) return MBEDTLS_ERR_X509_FATAL_ERROR;
    const bool matches = mbedtls_ct_memcmp(actual_sha256, pin->sha256,
                                           sizeof(actual_sha256)) == 0;
    memset(actual_sha256, 0, sizeof(actual_sha256));
    if (!matches) return MBEDTLS_ERR_X509_FATAL_ERROR;

    *flags &= ~MBEDTLS_X509_BADCERT_NOT_TRUSTED;
    return *flags == 0 ? 0 : MBEDTLS_ERR_X509_FATAL_ERROR;
}

static esp_err_t pve_pinned_leaf_attach(void *configuration)
{
    mbedtls_ssl_config *config = configuration;
    if (config == NULL || !s_pve_pin.ready) return ESP_ERR_INVALID_STATE;
    mbedtls_ssl_conf_authmode(config, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_verify(config, pve_pinned_leaf_verify, &s_pve_pin);
    return ESP_OK;
}

static bool prepare_pve_leaf_pin(const char *certificate_pem)
{
    memset(&s_pve_pin, 0, sizeof(s_pve_pin));
    mbedtls_x509_crt certificate;
    mbedtls_x509_crt_init(&certificate);
    int result = mbedtls_x509_crt_parse(&certificate,
                                        (const unsigned char *)certificate_pem,
                                        strlen(certificate_pem) + 1);
    bool is_leaf = result == 0 && certificate.ca_istrue == 0 &&
                   certificate.raw.p != NULL && certificate.raw.len > 0;
    if (is_leaf) {
        result = mbedtls_sha256_ret(certificate.raw.p, certificate.raw.len,
                                    s_pve_pin.sha256, 0);
        s_pve_pin.ready = result == 0;
    }
    mbedtls_x509_crt_free(&certificate);
    return is_leaf && s_pve_pin.ready;
}

static bool grow_http_buffer(char **buffer, size_t *capacity, size_t required)
{
    if (buffer == NULL || capacity == NULL || required == 0) return false;
    size_t next_capacity = *capacity > 0 ? *capacity : HTTP_INITIAL_CAPACITY;
    while (next_capacity < required) {
        if (next_capacity > SIZE_MAX / 2) return false;
        next_capacity *= 2;
    }
    char *replacement = realloc(*buffer, next_capacity);
    if (replacement == NULL) return false;
    *buffer = replacement;
    *capacity = next_capacity;
    return true;
}

static bool pve_get_json(const char *host, const char *common_name,
                         const char *token_header, const char *ca,
                         const char *path, bool post, cJSON **root_out,
                         char *error, size_t error_size)
{
    *root_out = NULL;
    if (ca == NULL || ca[0] == '\0') {
        snprintf(error, error_size, "PVE 未配置 CA 证书");
        return false;
    }

    char *body_buffer = NULL;
    size_t body_capacity = 0;
    if (!grow_http_buffer(&body_buffer, &body_capacity, HTTP_INITIAL_CAPACITY)) {
        snprintf(error, error_size, "PVE 响应缓冲区不足");
        return false;
    }
    body_buffer[0] = '\0';
    const bool use_leaf_pin = prepare_pve_leaf_pin(ca);
    esp_tls_cfg_t tls_config = {
        .timeout_ms = HTTP_TIMEOUT_MS,
        .common_name = common_name,
        .skip_common_name = false,
    };
    if (use_leaf_pin) {
        tls_config.crt_bundle_attach = pve_pinned_leaf_attach;
    } else {
        tls_config.cacert_buf = (const unsigned char *)ca;
        tls_config.cacert_bytes = strlen(ca) + 1;
    }
    esp_tls_t *tls = esp_tls_init();
    const int tls_result = tls == NULL ? -1 :
                           esp_tls_conn_new_sync(host, strlen(host), 8006,
                                                 &tls_config, tls);
    memset(&s_pve_pin, 0, sizeof(s_pve_pin));
    if (tls_result != 1) {
        if (tls != NULL) esp_tls_conn_destroy(tls);
        free(body_buffer);
        snprintf(error, error_size, "PVE TLS 连接失败");
        return false;
    }
    char request[512];
    int request_length = snprintf(request, sizeof(request),
                                  "%s %s HTTP/1.1\r\nHost: %s:8006\r\n"
                                  "Authorization: %s\r\nContent-Length: 0\r\n"
                                  "Connection: close\r\n\r\n",
                                  post ? "POST" : "GET", path, common_name, token_header);
    if (request_length <= 0 || request_length >= (int)sizeof(request)) {
        esp_tls_conn_destroy(tls);
        free(body_buffer);
        snprintf(error, error_size, "PVE 请求过长");
        return false;
    }
    size_t sent = 0;
    while (sent < (size_t)request_length) {
        ssize_t written = esp_tls_conn_write(tls, request + sent,
                                             (size_t)request_length - sent);
        if (written <= 0) break;
        sent += (size_t)written;
    }
    memset(request, 0, sizeof(request));
    size_t received = 0;
    bool response_complete = false;
    bool response_failed = false;
    const int64_t read_deadline_us = esp_timer_get_time() + HTTP_TIMEOUT_MS * 1000LL;
    while (sent == (size_t)request_length) {
        if (!grow_http_buffer(&body_buffer, &body_capacity, received + 2049)) {
            response_failed = true;
            break;
        }
        ssize_t count = esp_tls_conn_read(tls, body_buffer + received,
                                          body_capacity - received - 1);
        if (count == 0) {
            response_complete = true;
            break;
        }
        if ((count == MBEDTLS_ERR_SSL_WANT_READ || count == MBEDTLS_ERR_SSL_WANT_WRITE) &&
            esp_timer_get_time() < read_deadline_us) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (count < 0 || esp_timer_get_time() >= read_deadline_us) {
            response_failed = true;
            break;
        }
        received += (size_t)count;
    }
    esp_tls_conn_destroy(tls);
    if (sent != (size_t)request_length || response_failed || !response_complete) {
        snprintf(error, error_size, "PVE HTTP 响应未完整接收");
        free(body_buffer);
        return false;
    }
    body_buffer[received] = '\0';
    int status = 0;
    if (sscanf(body_buffer, "HTTP/%*u.%*u %d", &status) != 1 || status != 200) {
        snprintf(error, error_size, "PVE 请求失败 (%d)", status);
        free(body_buffer);
        return false;
    }
    char *payload = strstr(body_buffer, "\r\n\r\n");
    if (payload == NULL) {
        snprintf(error, error_size, "PVE HTTP 响应无效");
        free(body_buffer);
        return false;
    }
    payload += 4;
    size_t payload_length = received - (size_t)(payload - body_buffer);
    cJSON *root = cJSON_ParseWithLength(payload, payload_length);
    if (root == NULL) {
        snprintf(error, error_size, "PVE JSON 无效");
        free(body_buffer);
        return false;
    }
    *root_out = root;
    free(body_buffer);
    return true;
}

static uint64_t json_u64(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(value) && value->valuedouble > 0) {
        return (uint64_t)value->valuedouble;
    }
    return 0;
}

static double json_number(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(value) ? value->valuedouble : 0.0;
}

static const char *json_string(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) && value->valuestring != NULL ? value->valuestring : "";
}

static const cJSON *json_data(const cJSON *root)
{
    return cJSON_GetObjectItemCaseSensitive(root, "data");
}

static void parse_pve_node(const cJSON *root, app_snapshot_t *snapshot)
{
    const cJSON *data = json_data(root);
    if (!cJSON_IsObject(data)) return;
    snapshot->pve_online = true;
    snapshot->pve_cpu_percent = (float)(json_number(data, "cpu") * 100.0);
    const cJSON *load = cJSON_GetObjectItemCaseSensitive(data, "loadavg");
    for (size_t i = 0; i < 3; ++i) {
        const cJSON *entry = cJSON_IsArray(load) ? cJSON_GetArrayItem(load, (int)i) : NULL;
        if (cJSON_IsNumber(entry)) snapshot->pve_load[i] = (float)entry->valuedouble;
        else if (cJSON_IsString(entry) && entry->valuestring != NULL)
            snapshot->pve_load[i] = strtof(entry->valuestring, NULL);
        else snapshot->pve_load[i] = 0.0f;
    }
    const cJSON *cpuinfo = cJSON_GetObjectItem(data, "cpuinfo");
    snapshot->pve_cpu_cores = (uint32_t)json_u64(cpuinfo, "cpus");
    snapshot->pve_uptime_seconds = (uint32_t)json_u64(data, "uptime");
    copy_text(snapshot->pve_cpu_model, sizeof(snapshot->pve_cpu_model),
              json_string(cpuinfo, "model"));
    const cJSON *memory = cJSON_GetObjectItem(data, "memory");
    snapshot->pve_memory_used = json_u64(memory, "used");
    snapshot->pve_memory_total = json_u64(memory, "total");
    const cJSON *rootfs = cJSON_GetObjectItem(data, "rootfs");
    snapshot->pve_storage_used = json_u64(rootfs, "used");
    snapshot->pve_storage_total = json_u64(rootfs, "total");
    copy_text(snapshot->pve_version, sizeof(snapshot->pve_version), json_string(data, "pveversion"));
    const char *version = strstr(snapshot->pve_version, "pve-manager/");
    if (version != NULL) {
        memmove(snapshot->pve_version, version + strlen("pve-manager/"),
                strlen(version + strlen("pve-manager/")) + 1);
    }
    char *build_suffix = strchr(snapshot->pve_version, '/');
    if (build_suffix != NULL) *build_suffix = '\0';
}

static bool parse_pve_guests(const cJSON *root, app_snapshot_t *snapshot)
{
    const cJSON *data = json_data(root);
    if (!cJSON_IsArray(data)) return false;
    const int item_count = cJSON_GetArraySize(data);
    if (item_count < 0 ||
        !app_snapshot_reserve_pve_guests(snapshot, (size_t)item_count)) return false;
    snapshot->pve_guest_count = 0;
    snapshot->pve_running_count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, data) {
        if (!cJSON_IsObject(item)) return false;
        const char *kind = json_string(item, "type");
        if (strcmp(kind, "qemu") != 0 && strcmp(kind, "lxc") != 0) continue;
        pve_guest_t *guest = &snapshot->pve_guests[snapshot->pve_guest_count];
        memset(guest, 0, sizeof(*guest));
        guest->vmid = (uint32_t)json_u64(item, "vmid");
        copy_text(guest->name, sizeof(guest->name), json_string(item, "name"));
        copy_text(guest->kind, sizeof(guest->kind), kind);
        guest->running = strcmp(json_string(item, "status"), "running") == 0;
        guest->cpu_percent = (float)(json_number(item, "cpu") * 100.0);
        guest->memory_used = json_u64(item, "mem");
        guest->memory_total = json_u64(item, "maxmem");
        guest->disk_used = json_u64(item, "disk");
        guest->disk_total = json_u64(item, "maxdisk");
        guest->cpu_cores = (uint32_t)json_u64(item, "maxcpu");
        guest->uptime_seconds = (uint32_t)json_u64(item, "uptime");
        if (guest->running) snapshot->pve_running_count++;
        snapshot->pve_guest_count++;
    }
    return true;
}

static bool usable_ipv4(const char *address)
{
    struct in_addr parsed;
    if (address == NULL || inet_pton(AF_INET, address, &parsed) != 1) return false;
    const uint32_t host = ntohl(parsed.s_addr);
    return (host & 0xff000000U) != 0x7f000000U &&
           (host & 0xffff0000U) != 0xa9fe0000U && host != 0;
}

static bool copy_ipv4_without_cidr(const char *candidate, char *out, size_t out_size)
{
    if (candidate == NULL || out == NULL || out_size < 16) return false;
    char address[16] = {0};
    const char *slash = strchr(candidate, '/');
    const size_t length = slash != NULL ? (size_t)(slash - candidate) : strlen(candidate);
    if (length == 0 || length >= sizeof(address)) return false;
    memcpy(address, candidate, length);
    if (!usable_ipv4(address)) return false;
    copy_text(out, out_size, address);
    return true;
}

static bool select_guest_ipv4(const cJSON *root, bool qemu, char *out, size_t out_size)
{
    const cJSON *data = json_data(root);
    if (qemu && cJSON_IsObject(data)) {
        const cJSON *result = cJSON_GetObjectItemCaseSensitive(data, "result");
        if (cJSON_IsArray(result)) data = result;
    }
    if (!cJSON_IsArray(data)) return false;

    const cJSON *interface;
    cJSON_ArrayForEach(interface, data) {
        if (!cJSON_IsObject(interface)) continue;
        if (!qemu) {
            if (copy_ipv4_without_cidr(json_string(interface, "inet"), out, out_size))
                return true;
            continue;
        }
        const cJSON *addresses = cJSON_GetObjectItemCaseSensitive(interface, "ip-addresses");
        const cJSON *address;
        cJSON_ArrayForEach(address, addresses) {
            if (!cJSON_IsObject(address)) continue;
            const char *type = json_string(address, "ip-address-type");
            if (type[0] != '\0' && strcmp(type, "ipv4") != 0) continue;
            if (copy_ipv4_without_cidr(json_string(address, "ip-address"), out, out_size))
                return true;
        }
    }
    return false;
}

static bool collect_pve(const char *host, const char *node, const char *token_id,
                        const char *token_secret, const char *ca, app_snapshot_t *snapshot)
{
    char error[APP_TEXT_LARGE] = {0};
    if (token_secret[0] == '\0' || ca[0] == '\0') {
        snapshot->pve_configured = false;
        copy_text(snapshot->pve_last_error, sizeof(snapshot->pve_last_error),
                  "请在配置页填写 PVE Token Secret 和 CA");
        return false;
    }
    snapshot->pve_configured = true;
    char auth[180];
    snprintf(auth, sizeof(auth), "PVEAPIToken=%s=%s", token_id, token_secret);
    char path[128];
    snprintf(path, sizeof(path), "/api2/json/nodes/%s/status", node);
    cJSON *root = NULL;
    if (!pve_get_json(host, node, auth, ca, path, false, &root, error, sizeof(error))) {
        copy_text(snapshot->pve_last_error, sizeof(snapshot->pve_last_error), error);
        snapshot->pve_online = false;
        return false;
    }
    parse_pve_node(root, snapshot);
    cJSON_Delete(root);

    snprintf(path, sizeof(path), "/api2/json/cluster/resources?type=vm");
    root = NULL;
    if (!pve_get_json(host, node, auth, ca, path, false, &root, error, sizeof(error)) ||
        !parse_pve_guests(root, snapshot)) {
        if (root != NULL) cJSON_Delete(root);
        copy_text(snapshot->pve_last_error, sizeof(snapshot->pve_last_error),
                  error[0] != '\0' ? error : "PVE 虚拟机数据无效");
        snapshot->pve_online = false;
        return false;
    }
    cJSON_Delete(root);
    for (size_t i = 0; i < snapshot->pve_guest_count; ++i) {
        pve_guest_t *guest = &snapshot->pve_guests[i];
        if (!guest->running) continue;
        const bool qemu = strcmp(guest->kind, "qemu") == 0;
        if (qemu) {
            snprintf(path, sizeof(path),
                     "/api2/json/nodes/%s/qemu/%" PRIu32 "/agent/network-get-interfaces",
                     node, guest->vmid);
        } else {
            snprintf(path, sizeof(path), "/api2/json/nodes/%s/lxc/%" PRIu32 "/interfaces",
                     node, guest->vmid);
        }
        cJSON *agent_root = NULL;
        char agent_error[32] = {0};
        if (pve_get_json(host, node, auth, ca, path, false, &agent_root,
                         agent_error, sizeof(agent_error))) {
            guest->guest_agent = qemu;
            (void)select_guest_ipv4(agent_root, qemu, guest->ipv4_address,
                                    sizeof(guest->ipv4_address));
            cJSON_Delete(agent_root);
        }
    }
    snapshot->pve_online = true;
    snapshot->pve_last_error[0] = '\0';
    return true;
}

static size_t ber_write_length(uint8_t *out, size_t length)
{
    if (length < 128) {
        out[0] = (uint8_t)length;
        return 1;
    }
    if (length < 256) {
        out[0] = 0x81;
        out[1] = (uint8_t)length;
        return 2;
    }
    out[0] = 0x82;
    out[1] = (uint8_t)(length >> 8);
    out[2] = (uint8_t)length;
    return 3;
}

static size_t ber_append_tlv(uint8_t *out, size_t offset, uint8_t tag,
                             const uint8_t *value, size_t value_length)
{
    out[offset++] = tag;
    offset += ber_write_length(out + offset, value_length);
    memcpy(out + offset, value, value_length);
    return offset + value_length;
}

static size_t ber_append_integer(uint8_t *out, size_t offset, uint8_t tag, uint32_t value)
{
    uint8_t bytes[5];
    size_t count = 0;
    do {
        bytes[sizeof(bytes) - 1 - count++] = (uint8_t)value;
        value >>= 8;
    } while (value != 0);
    if ((bytes[sizeof(bytes) - count] & 0x80) != 0) {
        bytes[sizeof(bytes) - 1 - count++] = 0;
        bytes[sizeof(bytes) - count] = 0;
    }
    return ber_append_tlv(out, offset, tag, bytes + sizeof(bytes) - count, count);
}

static bool parse_oid(const char *text, uint32_t *numbers, size_t *count)
{
    char copy[96];
    copy_text(copy, sizeof(copy), text);
    size_t used = 0;
    char *cursor = copy;
    while (*cursor != '\0' && used < 32) {
        char *end = NULL;
        unsigned long number = strtoul(cursor, &end, 10);
        if (end == cursor) return false;
        numbers[used++] = (uint32_t)number;
        if (*end == '\0') break;
        if (*end != '.') return false;
        cursor = end + 1;
    }
    *count = used;
    return used >= 2;
}

static size_t encode_oid(uint8_t *out, const char *text)
{
    uint32_t numbers[32];
    size_t count = 0;
    if (!parse_oid(text, numbers, &count)) return 0;
    uint8_t encoded[96];
    size_t used = 0;
    encoded[used++] = (uint8_t)(numbers[0] * 40 + numbers[1]);
    for (size_t i = 2; i < count; ++i) {
        uint8_t parts[5];
        size_t part_count = 0;
        uint32_t value = numbers[i];
        do {
            parts[part_count++] = (uint8_t)(value & 0x7f);
            value >>= 7;
        } while (value != 0);
        while (part_count > 0) {
            uint8_t part = parts[--part_count];
            if (part_count > 0) part |= 0x80;
            encoded[used++] = part;
        }
    }
    return ber_append_tlv(out, 0, 0x06, encoded, used);
}

static size_t build_snmp_request(uint8_t *out, size_t capacity, const char *community,
                                 const char *oid, uint32_t request_id, bool get_next)
{
    uint8_t oid_tlv[128];
    size_t oid_length = encode_oid(oid_tlv, oid);
    if (oid_length == 0) return 0;
    uint8_t varbind_content[140];
    size_t varbind_content_length = 0;
    memcpy(varbind_content + varbind_content_length, oid_tlv, oid_length);
    varbind_content_length += oid_length;
    varbind_content[varbind_content_length++] = 0x05;
    varbind_content[varbind_content_length++] = 0x00;
    uint8_t varbind[160];
    size_t varbind_length = ber_append_tlv(varbind, 0, 0x30,
                                           varbind_content, varbind_content_length);
    uint8_t varbind_list[180];
    size_t varbind_list_length = ber_append_tlv(varbind_list, 0, 0x30,
                                                varbind, varbind_length);

    uint8_t pdu_content[240];
    size_t pdu_length = 0;
    pdu_length = ber_append_integer(pdu_content, pdu_length, 0x02, request_id);
    pdu_length = ber_append_integer(pdu_content, pdu_length, 0x02, 0);
    pdu_length = ber_append_integer(pdu_content, pdu_length, 0x02, 0);
    memcpy(pdu_content + pdu_length, varbind_list, varbind_list_length);
    pdu_length += varbind_list_length;
    uint8_t pdu[260];
    const uint8_t pdu_tag = get_next ? 0xA1 : 0xA0;
    size_t pdu_total = ber_append_tlv(pdu, 0, pdu_tag, pdu_content, pdu_length);

    uint8_t message_content[420];
    size_t message_length = 0;
    message_length = ber_append_integer(message_content, message_length, 0x02, 1);
    message_length = ber_append_tlv(message_content, message_length, 0x04,
                                    (const uint8_t *)community, strlen(community));
    memcpy(message_content + message_length, pdu, pdu_total);
    message_length += pdu_total;
    if (message_length + 4 > capacity) return 0;
    return ber_append_tlv(out, 0, 0x30, message_content, message_length);
}

static bool ber_read_tlv(const uint8_t *buffer, size_t length, size_t *offset,
                         uint8_t *tag, const uint8_t **value, size_t *value_length)
{
    if (*offset >= length) return false;
    *tag = buffer[(*offset)++];
    if (*offset >= length) return false;
    uint8_t first = buffer[(*offset)++];
    size_t decoded_length;
    if ((first & 0x80) == 0) {
        decoded_length = first;
    } else {
        const size_t octets = first & 0x7f;
        if (octets == 0 || octets > 2 || *offset + octets > length) return false;
        decoded_length = 0;
        for (size_t i = 0; i < octets; ++i) decoded_length = (decoded_length << 8) | buffer[(*offset)++];
    }
    if (*offset + decoded_length > length) return false;
    *value = buffer + *offset;
    *value_length = decoded_length;
    *offset += decoded_length;
    return true;
}

static uint64_t ber_read_number(const uint8_t *value, size_t length)
{
    uint64_t number = 0;
    for (size_t i = 0; i < length && i < sizeof(number); ++i) number = (number << 8) | value[i];
    return number;
}

static bool decode_oid(const uint8_t *value, size_t length, char *out, size_t out_size,
                       uint32_t *last_index)
{
    if (length == 0) return false;
    size_t written = (size_t)snprintf(out, out_size, "%u.%u", value[0] / 40, value[0] % 40);
    uint32_t last = 0;
    size_t i = 1;
    while (i < length) {
        uint32_t number = 0;
        uint8_t byte;
        do {
            byte = value[i++];
            number = (number << 7) | (byte & 0x7f);
        } while ((byte & 0x80) != 0 && i < length);
        if ((byte & 0x80) != 0) return false;
        last = number;
        if (written < out_size) written += (size_t)snprintf(out + written, out_size - written, ".%u", number);
    }
    if (last_index != NULL) *last_index = last;
    return written < out_size;
}

static bool parse_snmp_response(const uint8_t *buffer, size_t length,
                                uint32_t expected_request_id, snmp_value_t *value)
{
    size_t offset = 0;
    uint8_t tag;
    const uint8_t *message;
    size_t message_length;
    if (!ber_read_tlv(buffer, length, &offset, &tag, &message, &message_length) || tag != 0x30) return false;

    size_t message_offset = 0;
    const uint8_t *ignored;
    size_t ignored_length;
    if (!ber_read_tlv(message, message_length, &message_offset, &tag, &ignored, &ignored_length) || tag != 0x02) return false;
    if (!ber_read_tlv(message, message_length, &message_offset, &tag, &ignored, &ignored_length) || tag != 0x04) return false;

    const uint8_t *pdu;
    size_t pdu_length;
    if (!ber_read_tlv(message, message_length, &message_offset, &tag, &pdu, &pdu_length) ||
        tag != 0xA2) return false;
    size_t pdu_offset = 0;
    if (!ber_read_tlv(pdu, pdu_length, &pdu_offset, &tag, &ignored, &ignored_length) ||
        tag != 0x02) return false;
    const uint32_t response_request_id = (uint32_t)ber_read_number(ignored, ignored_length);
    if (response_request_id != expected_request_id) return false;
    if (!ber_read_tlv(pdu, pdu_length, &pdu_offset, &tag, &ignored, &ignored_length) ||
        tag != 0x02) return false;
    const uint64_t error_status = ber_read_number(ignored, ignored_length);
    if (error_status != 0) return false;
    if (!ber_read_tlv(pdu, pdu_length, &pdu_offset, &tag, &ignored, &ignored_length) ||
        tag != 0x02) return false;
    const uint8_t *bindings;
    size_t bindings_length;
    if (!ber_read_tlv(pdu, pdu_length, &pdu_offset, &tag, &bindings, &bindings_length) || tag != 0x30) return false;
    size_t bindings_offset = 0;
    const uint8_t *binding;
    size_t binding_length;
    if (!ber_read_tlv(bindings, bindings_length, &bindings_offset, &tag, &binding, &binding_length) || tag != 0x30) return false;

    size_t binding_offset = 0;
    const uint8_t *oid_value;
    size_t oid_length;
    if (!ber_read_tlv(binding, binding_length, &binding_offset, &tag, &oid_value, &oid_length) || tag != 0x06) return false;
    memset(value, 0, sizeof(*value));
    if (!decode_oid(oid_value, oid_length, value->oid, sizeof(value->oid), &value->last_index)) return false;
    const uint8_t *raw_value;
    size_t raw_length;
    if (!ber_read_tlv(binding, binding_length, &binding_offset, &tag, &raw_value, &raw_length)) return false;
    if (tag == 0x04 || tag == 0x0C) {
        value->type = SNMP_VALUE_STRING;
        size_t copied = raw_length < sizeof(value->text) - 1 ? raw_length : sizeof(value->text) - 1;
        memcpy(value->text, raw_value, copied);
        value->text[copied] = '\0';
    } else if (tag == 0x02) {
        value->type = SNMP_VALUE_INTEGER;
        value->number = ber_read_number(raw_value, raw_length);
    } else if (tag == 0x41 || tag == 0x42 || tag == 0x43 || tag == 0x46) {
        value->type = SNMP_VALUE_UNSIGNED;
        value->number = ber_read_number(raw_value, raw_length);
    } else {
        value->type = SNMP_VALUE_NONE;
    }
    return value->type != SNMP_VALUE_NONE;
}

static bool snmp_exchange(const char *host, const char *community, const char *oid,
                          bool get_next, snmp_value_t *value)
{
    struct sockaddr_in destination = {0};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(161);
    if (inet_pton(AF_INET, host, &destination.sin_addr) != 1) return false;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) return false;
    uint8_t request[512];
    static uint32_t request_id = 100;
    const uint32_t current_request_id = ++request_id;
    const size_t request_length = build_snmp_request(request, sizeof(request), community, oid,
                                                     current_request_id, get_next);
    bool success = false;
    if (request_length > 0 &&
        sendto(socket_fd, request, request_length, 0,
               (const struct sockaddr *)&destination, sizeof(destination)) >= 0) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(socket_fd, &read_set);
        struct timeval timeout = {
            .tv_sec = SNMP_TIMEOUT_MS / 1000,
            .tv_usec = (SNMP_TIMEOUT_MS % 1000) * 1000,
        };
        if (select(socket_fd + 1, &read_set, NULL, NULL, &timeout) > 0) {
            uint8_t response[SNMP_RESPONSE_MAX];
            const int received = recv(socket_fd, response, sizeof(response), 0);
            if (received > 0) {
                success = parse_snmp_response(response, (size_t)received,
                                              current_request_id, value);
            }
        }
    }
    close(socket_fd);
    return success;
}

static bool snmp_get_number(const char *host, const char *community, const char *oid, uint64_t *out)
{
    snmp_value_t value;
    if (!snmp_exchange(host, community, oid, false, &value)) return false;
    if (value.type != SNMP_VALUE_INTEGER && value.type != SNMP_VALUE_UNSIGNED) return false;
    *out = value.number;
    return true;
}

static bool snmp_get_text(const char *host, const char *community, const char *oid,
                          char *out, size_t out_size)
{
    snmp_value_t value;
    if (!snmp_exchange(host, community, oid, false, &value) || value.type != SNMP_VALUE_STRING) return false;
    copy_text(out, out_size, value.text);
    return true;
}

static void append_oid_index(char *out, size_t size, const char *base, uint32_t index)
{
    snprintf(out, size, "%s.%" PRIu32, base, index);
}

static bool oid_in_subtree(const char *oid, const char *base)
{
    const size_t base_length = strlen(base);
    return strncmp(oid, base, base_length) == 0 && oid[base_length] == '.';
}

static int snmp_oid_compare(const char *left, const char *right)
{
    uint32_t left_numbers[32];
    uint32_t right_numbers[32];
    size_t left_count = sizeof(left_numbers) / sizeof(left_numbers[0]);
    size_t right_count = sizeof(right_numbers) / sizeof(right_numbers[0]);
    if (!parse_oid(left, left_numbers, &left_count) ||
        !parse_oid(right, right_numbers, &right_count)) return 0;
    const size_t common = left_count < right_count ? left_count : right_count;
    for (size_t i = 0; i < common; ++i) {
        if (left_numbers[i] < right_numbers[i]) return -1;
        if (left_numbers[i] > right_numbers[i]) return 1;
    }
    if (left_count < right_count) return -1;
    if (left_count > right_count) return 1;
    return 0;
}

static bool synology_disk_healthy(uint64_t status)
{
    return status == 1;
}

static bool collect_nas_disks(const char *host, const char *community,
                              app_snapshot_t *snapshot)
{
    static const char *disk_id_oid = "1.3.6.1.4.1.6574.2.1.1.2";
    char cursor[96];
    copy_text(cursor, sizeof(cursor), disk_id_oid);
    snapshot->nas_disk_count = 0;

    while (true) {
        snmp_value_t value;
        if (!snmp_exchange(host, community, cursor, true, &value)) return false;
        if (!oid_in_subtree(value.oid, disk_id_oid)) return true;
        if (value.type != SNMP_VALUE_STRING || snmp_oid_compare(value.oid, cursor) <= 0)
            return false;
        copy_text(cursor, sizeof(cursor), value.oid);

        if (!app_snapshot_reserve_nas_disks(snapshot, snapshot->nas_disk_count + 1))
            return false;

        nas_disk_t *disk = &snapshot->nas_disks[snapshot->nas_disk_count];
        memset(disk, 0, sizeof(*disk));
        copy_text(disk->id, sizeof(disk->id), value.text);
        disk->capacity_valid = false;

        char oid[112];
        append_oid_index(oid, sizeof(oid), "1.3.6.1.4.1.6574.2.1.1.3",
                         value.last_index);
        (void)snmp_get_text(host, community, oid, disk->model, sizeof(disk->model));

        uint64_t status = 0;
        append_oid_index(oid, sizeof(oid), "1.3.6.1.4.1.6574.2.1.1.5",
                         value.last_index);
        disk->healthy = snmp_get_number(host, community, oid, &status) &&
                        synology_disk_healthy(status);

        uint64_t temperature = 0;
        append_oid_index(oid, sizeof(oid), "1.3.6.1.4.1.6574.2.1.1.6",
                         value.last_index);
        disk->temperature_valid = snmp_get_number(host, community, oid, &temperature) &&
                                  temperature <= 150;
        if (disk->temperature_valid) disk->temperature_c = (int)temperature;
        snapshot->nas_disk_count++;
    }
}

static bool interface_name_rejected(const char *name)
{
    static const char *const rejected_prefixes[] = {
        "lo", "docker", "veth", "virbr", "br-", "ovs-system", "tun", "tap", "sit",
    };
    if (name == NULL || name[0] == '\0') return true;
    for (size_t i = 0; i < sizeof(rejected_prefixes) / sizeof(rejected_prefixes[0]); ++i) {
        const size_t length = strlen(rejected_prefixes[i]);
        if (strncmp(name, rejected_prefixes[i], length) == 0) return true;
    }
    return false;
}

static int interface_name_rank(const char *name)
{
    if (strstr(name, "bond") != NULL) return 3;
    if (strncmp(name, "eth", 3) == 0 || strncmp(name, "en", 2) == 0 ||
        strncmp(name, "lan", 3) == 0) return 2;
    return 1;
}

static bool read_interface_counters(const char *host, const char *community, uint32_t index,
                                    uint64_t *rx_octets, uint64_t *tx_octets)
{
    char oid[112];
    append_oid_index(oid, sizeof(oid), "1.3.6.1.2.1.31.1.1.1.6", index);
    if (!snmp_get_number(host, community, oid, rx_octets)) return false;
    append_oid_index(oid, sizeof(oid), "1.3.6.1.2.1.31.1.1.1.10", index);
    return snmp_get_number(host, community, oid, tx_octets);
}

static void collect_nas_network(const char *host, const char *community, app_snapshot_t *snapshot)
{
    static const char *if_name_oid = "1.3.6.1.2.1.31.1.1.1.1";
    char cursor[96];
    copy_text(cursor, sizeof(cursor), if_name_oid);
    uint32_t selected_index = 0;
    uint64_t selected_rx = 0;
    uint64_t selected_tx = 0;
    int selected_rank = 0;

    for (size_t i = 0; i < 32; ++i) {
        snmp_value_t name;
        if (!snmp_exchange(host, community, cursor, true, &name) ||
            !oid_in_subtree(name.oid, if_name_oid) || name.type != SNMP_VALUE_STRING) break;
        copy_text(cursor, sizeof(cursor), name.oid);
        if (interface_name_rejected(name.text)) continue;
        char status_oid[112];
        uint64_t status = 0;
        append_oid_index(status_oid, sizeof(status_oid), "1.3.6.1.2.1.2.2.1.8",
                         name.last_index);
        if (!snmp_get_number(host, community, status_oid, &status) || status != 1) continue;
        uint64_t rx = 0;
        uint64_t tx = 0;
        if (!read_interface_counters(host, community, name.last_index, &rx, &tx)) continue;
        const int rank = interface_name_rank(name.text);
        if (rank > selected_rank) {
            selected_rank = rank;
            selected_index = name.last_index;
            selected_rx = rx;
            selected_tx = tx;
        }
    }

    snapshot->nas_network_rate_valid = false;
    if (selected_index == 0) {
        s_nas_rate_baseline.valid = false;
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if (s_nas_rate_baseline.valid &&
        s_nas_rate_baseline.interface_index == selected_index &&
        selected_rx >= s_nas_rate_baseline.rx_octets &&
        selected_tx >= s_nas_rate_baseline.tx_octets) {
        const int64_t elapsed_us = now_us - s_nas_rate_baseline.sampled_at_us;
        if (elapsed_us >= 250000) {
            snapshot->nas_rx_bytes_per_second =
                (selected_rx - s_nas_rate_baseline.rx_octets) * 1000000ULL /
                (uint64_t)elapsed_us;
            snapshot->nas_tx_bytes_per_second =
                (selected_tx - s_nas_rate_baseline.tx_octets) * 1000000ULL /
                (uint64_t)elapsed_us;
            snapshot->nas_network_rate_valid = true;
        }
    }
    s_nas_rate_baseline.interface_index = selected_index;
    s_nas_rate_baseline.rx_octets = selected_rx;
    s_nas_rate_baseline.tx_octets = selected_tx;
    s_nas_rate_baseline.sampled_at_us = now_us;
    s_nas_rate_baseline.valid = true;
}

static const char *synology_raid_status(uint64_t status)
{
    switch (status) {
        case 1: return "正常";
        case 2: return "修复中";
        case 3: return "迁移中";
        case 4: return "扩容中";
        case 5: return "删除中";
        case 6: return "创建中";
        case 7: return "同步中";
        case 8: return "校验中";
        case 9: return "组装中";
        case 10: return "取消中";
        case 11: return "降级";
        case 12: return "故障";
        default: return NULL;
    }
}

static bool collect_nas(const char *host, const char *community, app_snapshot_t *snapshot)
{
    snapshot->nas_cpu_valid = false;
    snapshot->nas_memory_valid = false;
    snapshot->nas_temperature_valid = false;
    snapshot->nas_cpu_percent = 0;
    snapshot->nas_memory_percent = 0;
    snapshot->nas_temperature = 0;
    if (community[0] == '\0') {
        snapshot->nas_configured = false;
        copy_text(snapshot->nas_last_error, sizeof(snapshot->nas_last_error),
                  "请在配置页填写只读 SNMP Community");
        return false;
    }
    snapshot->nas_configured = true;
    struct in_addr nas_address;
    if (inet_pton(AF_INET, host, &nas_address) != 1) {
        copy_text(snapshot->nas_last_error, sizeof(snapshot->nas_last_error), "NAS IP 无效");
        snapshot->nas_online = false;
        return false;
    }
    uint64_t ticks = 0;
    snapshot->nas_uptime_valid = snmp_get_number(
        host, community, "1.3.6.1.2.1.25.1.1.0", &ticks);
    if (!snapshot->nas_uptime_valid) {
        snapshot->nas_uptime_valid = snmp_get_number(
            host, community, "1.3.6.1.2.1.1.3.0", &ticks);
    }
    snapshot->nas_uptime_seconds = snapshot->nas_uptime_valid ?
                                   (uint32_t)(ticks / 100ULL) : 0;
    uint64_t idle = 0;
    uint64_t user = 0;
    uint64_t system = 0;
    uint64_t memory_total = 0;
    uint64_t memory_available = 0;
    uint64_t memory_buffer = 0;
    uint64_t memory_cache = 0;
    uint64_t temperature = 0;
    const bool idle_ok = snmp_get_number(
        host, community, "1.3.6.1.4.1.2021.11.11.0", &idle);
    if (idle_ok && idle <= 100) {
        snapshot->nas_cpu_valid = true;
        snapshot->nas_cpu_percent = (float)(100 - idle);
    } else {
        const bool user_ok = snmp_get_number(
            host, community, "1.3.6.1.4.1.2021.11.9.0", &user);
        const bool system_ok = snmp_get_number(
            host, community, "1.3.6.1.4.1.2021.11.10.0", &system);
        const bool fallback_ok = user_ok && system_ok &&
                                 user <= 100 && system <= 100 && user + system <= 100;
        snapshot->nas_cpu_valid = fallback_ok;
        if (fallback_ok) snapshot->nas_cpu_percent = (float)(user + system);
    }
    const bool memory_ok = snmp_get_number(host, community, "1.3.6.1.4.1.2021.4.5.0", &memory_total) &&
                           snmp_get_number(host, community, "1.3.6.1.4.1.2021.4.6.0", &memory_available);
    const bool memory_buffer_ok = snmp_get_number(
        host, community, "1.3.6.1.4.1.2021.4.14.0", &memory_buffer);
    const bool memory_cache_ok = snmp_get_number(
        host, community, "1.3.6.1.4.1.2021.4.15.0", &memory_cache);
    snapshot->nas_memory_valid = memory_ok &&
                                 memory_total > 0 && memory_available <= memory_total;
    if (snapshot->nas_memory_valid) {
        uint64_t memory_used = memory_total - memory_available;
        if (memory_buffer_ok && memory_cache_ok &&
            memory_buffer <= UINT64_MAX - memory_cache) {
            const uint64_t memory_reclaimable = memory_buffer + memory_cache;
            if (memory_reclaimable <= memory_total - memory_available) {
                memory_used = memory_total - memory_available - memory_reclaimable;
            }
        }
        snapshot->nas_memory_percent =
            (float)(100.0 * (double)memory_used / memory_total);
    }
    const bool temperature_ok = snmp_get_number(
        host, community, "1.3.6.1.4.1.6574.1.2.0", &temperature);
    snapshot->nas_temperature_valid = temperature_ok && temperature <= 150;
    if (snapshot->nas_temperature_valid) snapshot->nas_temperature = (int)temperature;
    (void)snmp_get_text(host, community, "1.3.6.1.4.1.6574.1.5.0", snapshot->nas_model,
                        sizeof(snapshot->nas_model));

    static const char *raid_name_oid = "1.3.6.1.4.1.6574.3.1.1.2";
    snapshot->nas_pool_count = 0;
    char cursor[96];
    copy_text(cursor, sizeof(cursor), raid_name_oid);
    while (true) {
        snmp_value_t value;
        if (!snmp_exchange(host, community, cursor, true, &value)) return false;
        if (!oid_in_subtree(value.oid, raid_name_oid)) break;
        if (value.type != SNMP_VALUE_STRING || snmp_oid_compare(value.oid, cursor) <= 0)
            return false;
        copy_text(cursor, sizeof(cursor), value.oid);
        if (strncmp(value.text, "Volume ", 7) != 0) continue;
        if (!app_snapshot_reserve_nas_pools(snapshot, snapshot->nas_pool_count + 1))
            return false;
        nas_pool_t *pool = &snapshot->nas_pools[snapshot->nas_pool_count];
        memset(pool, 0, sizeof(*pool));
        copy_text(pool->name, sizeof(pool->name), value.text);
        char oid[112];
        append_oid_index(oid, sizeof(oid), "1.3.6.1.4.1.6574.3.1.1.5", value.last_index);
        if (!snmp_get_number(host, community, oid, &pool->total_bytes)) return false;
        append_oid_index(oid, sizeof(oid), "1.3.6.1.4.1.6574.3.1.1.4", value.last_index);
        if (!snmp_get_number(host, community, oid, &pool->free_bytes)) return false;
        if (pool->total_bytes > 0 && pool->total_bytes < 100000000000ULL) {
            pool->total_bytes *= 1024ULL * 1024ULL;
            pool->free_bytes *= 1024ULL * 1024ULL;
        }
        pool->used_bytes = pool->total_bytes > pool->free_bytes ?
                           pool->total_bytes - pool->free_bytes : 0;
        uint64_t raid_status = 0;
        append_oid_index(oid, sizeof(oid), "1.3.6.1.4.1.6574.3.1.1.3", value.last_index);
        if (!snmp_get_number(host, community, oid, &raid_status)) return false;
        pool->healthy = raid_status == 1;
        const char *status_text = synology_raid_status(raid_status);
        if (status_text != NULL) copy_text(pool->description, sizeof(pool->description), status_text);
        else snprintf(pool->description, sizeof(pool->description), "状态 %" PRIu64, raid_status);
        snapshot->nas_pool_count++;
    }
    if (!collect_nas_disks(host, community, snapshot)) return false;
    collect_nas_network(host, community, snapshot);
    const bool success = snapshot->nas_uptime_valid || snapshot->nas_cpu_valid ||
                         snapshot->nas_memory_valid || snapshot->nas_temperature_valid ||
                         snapshot->nas_pool_count > 0 || snapshot->nas_disk_count > 0;
    if (!success) {
        copy_text(snapshot->nas_last_error, sizeof(snapshot->nas_last_error), "SNMP 无响应或 OID 不匹配");
        snapshot->nas_online = false;
    } else {
        snapshot->nas_online = true;
        snapshot->nas_last_error[0] = '\0';
    }
    return success;
}

static void set_clock(app_snapshot_t *snapshot)
{
    time_t now = time(NULL);
    struct tm local_now = {0};
    localtime_r(&now, &local_now);
    snapshot->hour = (uint8_t)local_now.tm_hour;
    snapshot->minute = (uint8_t)local_now.tm_min;
    snapshot->second = (uint8_t)local_now.tm_sec;
}

static void publish_event(app_model_event_t event)
{
    if (s_snapshot_queue == NULL) {
        app_snapshot_destroy(event.snapshot);
        return;
    }
    if (xQueueSend(s_snapshot_queue, &event, 0) == pdTRUE) return;

    app_model_event_t dropped = {0};
    if (xQueueReceive(s_snapshot_queue, &dropped, 0) == pdTRUE) {
        app_snapshot_destroy(dropped.snapshot);
    }
    if (xQueueSend(s_snapshot_queue, &event, 0) != pdTRUE) {
        app_snapshot_destroy(event.snapshot);
    }
}

static void publish_status(app_model_event_kind_t kind, app_monitor_t monitor,
                           const char *error)
{
    app_model_event_t event = {
        .kind = kind,
        .monitor = monitor,
    };
    copy_text(event.error, sizeof(event.error), error);
    publish_event(event);
}

static void publish_collection_failure(app_monitor_t monitor, bool has_snapshot,
                                       const char *error, TickType_t request_started)
{
    if (has_snapshot) {
        publish_status(APP_MODEL_EVENT_REFRESH_FAILED, monitor, error);
        return;
    }
    const TickType_t timeout = pdMS_TO_TICKS(INITIAL_REQUEST_TIMEOUT_MS);
    const TickType_t elapsed = xTaskGetTickCount() - request_started;
    if (elapsed < timeout) vTaskDelay(timeout - elapsed);
    publish_status(APP_MODEL_EVENT_OFFLINE, monitor, error);
}

static bool publish_snapshot_clone(const app_snapshot_t *snapshot, app_monitor_t monitor)
{
    app_snapshot_t *published = app_snapshot_create();
    if (published == NULL || !app_snapshot_clone(published, snapshot)) {
        app_snapshot_destroy(published);
        return false;
    }
    app_model_event_t event = {
        .kind = APP_MODEL_EVENT_SNAPSHOT,
        .monitor = monitor,
        .snapshot = published,
    };
    publish_event(event);
    return true;
}

static void live_provider_task(void *argument)
{
    (void)argument;
    static app_snapshot_t snapshot;
    static char pve_host[32], pve_node[32], token_id[64], token_secret[96], pve_ca[4096];
    static char nas_host[32], community[64];
    app_snapshot_init(&snapshot);
    bool pve_has_snapshot = false;
    bool nas_has_snapshot = false;
    app_monitor_t startup_primary_monitor = APP_MONITOR_NONE;
    app_monitor_t startup_prefetch_monitor = APP_MONITOR_NONE;
    bool startup_primary_complete = false;
    bool startup_prefetch_complete = false;
    uint32_t revision = 0;
    ESP_LOGI(TAG, "Live PVE/NAS provider started; startup prefetch then active-page polling");
    while (true) {
        provider_control_t control = {
            .active_monitor = APP_MONITOR_NAS,
            .refresh_seconds = DEFAULT_REFRESH_SECONDS,
        };
        (void)xQueuePeek(s_control_queue, &control, 0);
        if (startup_primary_monitor == APP_MONITOR_NONE) {
            startup_primary_monitor = control.active_monitor == APP_MONITOR_PVE ?
                                      APP_MONITOR_PVE : APP_MONITOR_NAS;
            startup_prefetch_monitor = startup_primary_monitor == APP_MONITOR_NAS ?
                                       APP_MONITOR_PVE : APP_MONITOR_NAS;
        }
        app_monitor_t cycle_monitor = control.active_monitor;
        if (!startup_primary_complete) cycle_monitor = startup_primary_monitor;
        else if (!startup_prefetch_complete) cycle_monitor = startup_prefetch_monitor;
        const TickType_t cycle_started = xTaskGetTickCount();
        load_monitor_settings(pve_host, pve_node, token_id, token_secret, pve_ca,
                               nas_host, community);
        const bool cycle_has_snapshot = cycle_monitor == APP_MONITOR_PVE ?
            pve_has_snapshot : cycle_monitor == APP_MONITOR_NAS ? nas_has_snapshot : true;
        if (!cycle_has_snapshot)
            publish_status(APP_MODEL_EVENT_LOADING, cycle_monitor, NULL);
        if (!network_manager_is_connected()) {
            memset(token_id, 0, sizeof(token_id));
            memset(token_secret, 0, sizeof(token_secret));
            memset(pve_ca, 0, sizeof(pve_ca));
            memset(community, 0, sizeof(community));
            (void)network_manager_wait_for_connection_or_settings_change(
                pdMS_TO_TICKS(INITIAL_REQUEST_TIMEOUT_MS));
            continue;
        }
        app_snapshot_t *candidate = app_snapshot_create();
        const bool cloned = candidate != NULL && app_snapshot_clone(candidate, &snapshot);
        if (!cloned) {
            app_snapshot_destroy(candidate);
            publish_collection_failure(cycle_monitor, cycle_has_snapshot,
                                       "内存不足，无法刷新数据", cycle_started);
        } else {
            candidate->revision = ++revision;
            candidate->uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000ULL);
            candidate->wifi_connected = network_manager_is_connected();
            network_manager_get_ip(candidate->ip_address, sizeof(candidate->ip_address));
            copy_text(candidate->pve_host, sizeof(candidate->pve_host), pve_host);
            copy_text(candidate->pve_name, sizeof(candidate->pve_name), pve_node);
            copy_text(candidate->nas_host, sizeof(candidate->nas_host), nas_host);
            set_clock(candidate);
        }
        const bool cycle_connected = cloned && candidate->wifi_connected;

        if (cloned && cycle_monitor == APP_MONITOR_PVE) {
            bool pve_ok = false;
            if (candidate->wifi_connected) {
                pve_ok = collect_pve(pve_host, pve_node, token_id, token_secret,
                                     pve_ca, candidate);
            } else {
                candidate->pve_online = false;
                copy_text(candidate->pve_last_error, sizeof(candidate->pve_last_error),
                          "Wi-Fi 未连接");
            }
            if (pve_ok) {
                candidate->pve_stale = false;
                candidate->pve_last_success_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
                app_snapshot_move(&snapshot, candidate);
                app_snapshot_destroy(candidate);
                pve_has_snapshot = true;
                if (!publish_snapshot_clone(&snapshot, APP_MONITOR_PVE)) {
                    publish_status(APP_MODEL_EVENT_REFRESH_FAILED, APP_MONITOR_PVE,
                                   "内存不足，无法发布数据");
                }
                ESP_LOGI(TAG, "PVE refresh succeeded: guests=%zu", snapshot.pve_guest_count);
            } else {
                publish_collection_failure(APP_MONITOR_PVE, pve_has_snapshot,
                                           candidate->pve_last_error, cycle_started);
                ESP_LOGW(TAG, "PVE refresh failed: %s", candidate->pve_last_error);
                app_snapshot_destroy(candidate);
            }
        } else if (cloned && cycle_monitor == APP_MONITOR_NAS) {
            bool nas_ok = false;
            if (candidate->wifi_connected) {
                nas_ok = collect_nas(nas_host, community, candidate);
            } else {
                candidate->nas_online = false;
                copy_text(candidate->nas_last_error, sizeof(candidate->nas_last_error),
                          "Wi-Fi 未连接");
            }
            if (nas_ok) {
                candidate->nas_stale = false;
                candidate->nas_last_success_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
                app_snapshot_move(&snapshot, candidate);
                app_snapshot_destroy(candidate);
                nas_has_snapshot = true;
                uint32_t model_count = 0;
                uint32_t temperature_count = 0;
                for (size_t i = 0; i < snapshot.nas_disk_count; ++i) {
                    if (snapshot.nas_disks[i].model[0] != '\0') model_count++;
                    if (snapshot.nas_disks[i].temperature_valid) temperature_count++;
                }
                if (!publish_snapshot_clone(&snapshot, APP_MONITOR_NAS)) {
                    publish_status(APP_MODEL_EVENT_REFRESH_FAILED, APP_MONITOR_NAS,
                                   "内存不足，无法发布数据");
                }
                ESP_LOGI(TAG, "NAS refresh succeeded: pools=%zu"
                              " disks=%zu uptime=%s"
                              " models=%" PRIu32 " temperatures=%" PRIu32,
                         snapshot.nas_pool_count, snapshot.nas_disk_count,
                         snapshot.nas_uptime_valid ? "yes" : "no",
                         model_count, temperature_count);
            } else {
                publish_collection_failure(APP_MONITOR_NAS, nas_has_snapshot,
                                           candidate->nas_last_error, cycle_started);
                ESP_LOGW(TAG, "NAS refresh failed: %s", candidate->nas_last_error);
                app_snapshot_destroy(candidate);
            }
        } else if (cloned) {
            app_snapshot_destroy(candidate);
        }
        bool continue_startup_immediately = false;
        if (cycle_connected && !startup_primary_complete &&
            cycle_monitor == startup_primary_monitor) {
            startup_primary_complete = true;
            continue_startup_immediately = true;
            ESP_LOGI(TAG, "Startup primary refresh finished: monitor=%s",
                     cycle_monitor == APP_MONITOR_PVE ? "PVE" : "NAS");
        } else if (cycle_connected && startup_primary_complete &&
                   !startup_prefetch_complete &&
                   cycle_monitor == startup_prefetch_monitor) {
            startup_prefetch_complete = true;
            ESP_LOGI(TAG, "Startup prefetch finished: monitor=%s; active-page polling enabled",
                     cycle_monitor == APP_MONITOR_PVE ? "PVE" : "NAS");
        }
        memset(token_id, 0, sizeof(token_id));
        memset(token_secret, 0, sizeof(token_secret));
        memset(pve_ca, 0, sizeof(pve_ca));
        memset(community, 0, sizeof(community));
        const TickType_t elapsed = xTaskGetTickCount() - cycle_started;
        const TickType_t interval = pdMS_TO_TICKS((uint32_t)control.refresh_seconds * 1000U);
        const TickType_t wait = continue_startup_immediately ? 0 :
                                elapsed < interval ? interval - elapsed : 0;
        (void)network_manager_wait_for_settings_change(wait);
    }
}

void app_model_set_active_monitor(app_monitor_t monitor)
{
    if (s_control_queue == NULL) return;
    if (monitor > APP_MONITOR_PVE) monitor = APP_MONITOR_NONE;
    provider_control_t control = {
        .active_monitor = APP_MONITOR_NAS,
        .refresh_seconds = DEFAULT_REFRESH_SECONDS,
    };
    (void)xQueuePeek(s_control_queue, &control, 0);
    control.active_monitor = monitor;
    xQueueOverwrite(s_control_queue, &control);
    network_manager_notify_settings_changed();
}

void app_model_set_refresh_seconds(uint8_t seconds)
{
    if (s_control_queue == NULL || !valid_refresh_seconds(seconds)) return;
    provider_control_t control = {
        .active_monitor = APP_MONITOR_NAS,
        .refresh_seconds = DEFAULT_REFRESH_SECONDS,
    };
    (void)xQueuePeek(s_control_queue, &control, 0);
    control.refresh_seconds = seconds;
    xQueueOverwrite(s_control_queue, &control);
    network_manager_notify_settings_changed();
}

QueueHandle_t app_model_start_live_provider(void)
{
    s_snapshot_queue = xQueueCreate(1, sizeof(app_model_event_t));
    if (s_snapshot_queue == NULL) {
        ESP_LOGE(TAG, "Unable to allocate live snapshot queue");
        return NULL;
    }
    s_control_queue = xQueueCreate(1, sizeof(provider_control_t));
    if (s_control_queue == NULL) {
        vQueueDelete(s_snapshot_queue);
        s_snapshot_queue = NULL;
        ESP_LOGE(TAG, "Unable to allocate provider control queue");
        return NULL;
    }
    uint8_t refresh_seconds = DEFAULT_REFRESH_SECONDS;
    if (device_settings_get_u8(DEVICE_KEY_REFRESH, &refresh_seconds) != ESP_OK ||
        !valid_refresh_seconds(refresh_seconds)) {
        refresh_seconds = DEFAULT_REFRESH_SECONDS;
    }
    uint8_t saved_homepage = 0;
    if (device_settings_get_u8(DEVICE_KEY_HOME_PAGE, &saved_homepage) != ESP_OK ||
        saved_homepage > 1) saved_homepage = 0;
    provider_control_t control = {
        .active_monitor = saved_homepage == 1 ? APP_MONITOR_PVE : APP_MONITOR_NAS,
        .refresh_seconds = refresh_seconds,
    };
    xQueueOverwrite(s_control_queue, &control);
    if (xTaskCreatePinnedToCore(live_provider_task, "live_provider", 16 * 1024,
                                NULL, 3, NULL, 0) != pdPASS) {
        vQueueDelete(s_control_queue);
        s_control_queue = NULL;
        vQueueDelete(s_snapshot_queue);
        s_snapshot_queue = NULL;
        return NULL;
    }
    return s_snapshot_queue;
}
