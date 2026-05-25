#include <stdbool.h>
#include <string.h>

#include <rtthread.h>
#include <rtdevice.h>

#include "cJSON.h"
#include "gd32vw55x_ble.h"
#include "gd32vw55x_ble_datatrans.h"

#ifdef RT_USING_NETDEV
#include <netdev.h>
#endif

#ifdef RT_USING_LWIP
#include "lwip/netif.h"
#endif

#define GD_PROV_BLE_NAME              "GD32VW553H"
#define GD_PROV_DEFAULT_SSID          "123"
#define GD_PROV_DEFAULT_PASSWORD      "liuyucai"
#define GD_PROV_MAX_SSID_LEN          32
#define GD_PROV_MAX_PASSWORD_LEN      64
#define GD_PROV_JSON_BUFFER_LEN       256

volatile int gd_prov_ble_init_ret;
volatile int gd_prov_ble_datatrans_init_ret;
volatile int gd_prov_ble_connected;
volatile int gd_prov_ble_conn_idx = -1;
volatile int gd_prov_ble_rx_count;
volatile int gd_prov_ble_tx_count;
volatile int gd_prov_wifi_connect_count;
volatile int gd_prov_wifi_last_status;
volatile int gd_prov_json_ok_count;
volatile int gd_prov_json_error_count;

static rt_sem_t gd_prov_sem;
static char gd_prov_ssid[GD_PROV_MAX_SSID_LEN + 1];
static char gd_prov_password[GD_PROV_MAX_PASSWORD_LEN + 1];
static char gd_prov_json_buf[GD_PROV_JSON_BUFFER_LEN];
static uint16_t gd_prov_json_len;

rt_weak int gd32vw55x_wifi_ready(void)
{
    return 1;
}

static void gd_prov_notify(uint8_t conn_idx, const char *text)
{
    int ret;

    if (text == RT_NULL || gd_prov_ble_connected == 0) {
        return;
    }

    ret = gd32vw55x_ble_datatrans_send(conn_idx, text, (uint16_t)rt_strlen(text));
    if (ret == 0) {
        gd_prov_ble_tx_count++;
    }
}

static int gd_prov_wait_wlan_ready(void)
{
#ifdef RT_USING_WIFI
    int i;

    for (i = 0; i < 300; i++) {
        if ((rt_device_find(RT_WLAN_DEVICE_STA_NAME) != RT_NULL) &&
            gd32vw55x_wifi_ready()) {
            return 0;
        }
        rt_thread_mdelay(100);
    }
#endif

    return -RT_ETIMEOUT;
}

static rt_err_t gd_prov_wait_ip_text(char *buf, rt_size_t len)
{
    int i;

    if (buf == RT_NULL || len == 0) {
        return -RT_EINVAL;
    }

    rt_strncpy(buf, "0.0.0.0", len);
    buf[len - 1] = '\0';

    for (i = 0; i < 300; i++) {
#ifdef RT_USING_NETDEV
        {
            struct netdev *netdev = RT_NULL;

            netdev = netdev_get_by_name(RT_WLAN_DEVICE_STA_NAME);
            if (netdev != RT_NULL && netdev->ip_addr.addr != 0) {
                inet_ntoa_r(netdev->ip_addr, buf, (int)len);
                return RT_EOK;
            }
        }
#endif
#ifdef RT_USING_LWIP
        {
            const ip4_addr_t *ip4 = RT_NULL;

            if (netif_default != RT_NULL) {
                ip4 = netif_ip4_addr(netif_default);
            }

            if (ip4 != RT_NULL && !ip4_addr_isany_val(*ip4)) {
                rt_snprintf(buf, len, "%u.%u.%u.%u",
                            ip4_addr1_16(ip4), ip4_addr2_16(ip4),
                            ip4_addr3_16(ip4), ip4_addr4_16(ip4));
                return RT_EOK;
            }
        }
#endif
        rt_thread_mdelay(100);
    }

    return -RT_ETIMEOUT;
}

static int gd_prov_connect_wifi(const char *ssid, const char *password)
{
#ifdef RT_USING_WIFI
    rt_err_t ret;
    char ip[16];
    struct rt_wlan_info info;
    int i;

    ret = gd_prov_wait_wlan_ready();
    if (ret != RT_EOK) {
        rt_kprintf("[ble_prov] WLAN device is not ready: %d\n", ret);
        return ret;
    }

    rt_kprintf("[ble_prov] connect WiFi SSID=%s pwd_len=%u\n",
               ssid, (unsigned int)rt_strlen(password));

    rt_memset(&info, 0, sizeof(info));
    rt_strncpy((char *)info.ssid.val, ssid, sizeof(info.ssid.val));
    info.ssid.len = (rt_uint8_t)rt_strlen(ssid);
    info.security = SECURITY_UNKNOWN;

    ret = rt_wlan_connect_adv(&info, password);
    gd_prov_wifi_last_status = ret;
    if (ret != RT_EOK) {
        rt_kprintf("[ble_prov] WiFi connect failed: %d\n", ret);
        return ret;
    }

    for (i = 0; i < 300; i++) {
        if (rt_wlan_is_connected()) {
            break;
        }
        rt_thread_mdelay(100);
    }

    if (!rt_wlan_is_connected()) {
        ret = -RT_ETIMEOUT;
        gd_prov_wifi_last_status = ret;
        rt_kprintf("[ble_prov] WiFi connect timeout\n");
        return ret;
    }

    ret = gd_prov_wait_ip_text(ip, sizeof(ip));
    if (ret != RT_EOK) {
        gd_prov_wifi_last_status = ret;
        rt_kprintf("[ble_prov] WiFi DHCP/IP timeout\n");
        return ret;
    }

    rt_kprintf("[ble_prov] WiFi connected: SSID=%s IP=%s\n", ssid, ip);
    return RT_EOK;
#else
    (void)ssid;
    (void)password;
    return -RT_ENOSYS;
#endif
}

static void gd_prov_store_credentials(const char *ssid, const char *password)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    rt_strncpy(gd_prov_ssid, ssid, sizeof(gd_prov_ssid));
    rt_strncpy(gd_prov_password, password, sizeof(gd_prov_password));
    gd_prov_ssid[GD_PROV_MAX_SSID_LEN] = '\0';
    gd_prov_password[GD_PROV_MAX_PASSWORD_LEN] = '\0';
    rt_hw_interrupt_enable(level);
}

static void gd_prov_submit_json(const char *json, uint8_t conn_idx)
{
    cJSON *root;
    cJSON *ssid_item;
    cJSON *password_item;
    const char *ssid;
    const char *password;
    size_t ssid_len;
    size_t password_len;

    rt_kprintf("[ble_prov] BLE provisioning JSON: %s\n", json);

    root = cJSON_Parse(json);
    if (root == RT_NULL) {
        gd_prov_json_error_count++;
        rt_kprintf("[ble_prov] JSON parse failed\n");
        gd_prov_notify(conn_idx, "{\"status\":\"bad_json\"}\n");
        return;
    }

    ssid_item = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    password_item = cJSON_GetObjectItemCaseSensitive(root, "pwd");
    if (!cJSON_IsString(password_item)) {
        password_item = cJSON_GetObjectItemCaseSensitive(root, "password");
    }

    if (!cJSON_IsString(ssid_item) || !cJSON_IsString(password_item) ||
        ssid_item->valuestring == RT_NULL || password_item->valuestring == RT_NULL) {
        gd_prov_json_error_count++;
        rt_kprintf("[ble_prov] JSON missing ssid/pwd\n");
        gd_prov_notify(conn_idx, "{\"status\":\"missing_ssid_or_pwd\"}\n");
        cJSON_Delete(root);
        return;
    }

    ssid = ssid_item->valuestring;
    password = password_item->valuestring;
    ssid_len = rt_strlen(ssid);
    password_len = rt_strlen(password);

    if (ssid_len == 0 || ssid_len > GD_PROV_MAX_SSID_LEN ||
        password_len > GD_PROV_MAX_PASSWORD_LEN) {
        gd_prov_json_error_count++;
        rt_kprintf("[ble_prov] invalid credential length: ssid=%u pwd=%u\n",
                   (unsigned int)ssid_len, (unsigned int)password_len);
        gd_prov_notify(conn_idx, "{\"status\":\"invalid_length\"}\n");
        cJSON_Delete(root);
        return;
    }

    rt_kprintf("[ble_prov] received credentials: SSID=%s pwd_len=%u\n",
               ssid, (unsigned int)password_len);

    gd_prov_store_credentials(ssid, password);
    gd_prov_json_ok_count++;
    gd_prov_notify(conn_idx, "{\"status\":\"accepted\"}\n");
    rt_sem_release(gd_prov_sem);
    cJSON_Delete(root);
}

static void gd_prov_ble_rx(uint8_t conn_idx, uint16_t data_len, uint8_t *data)
{
    bool submit_now = false;
    uint16_t i;

    gd_prov_ble_rx_count++;

    for (i = 0; i < data_len; i++) {
        char ch = (char)data[i];

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n' || ch == '\0') {
            submit_now = (gd_prov_json_len > 0);
        } else if (gd_prov_json_len < (sizeof(gd_prov_json_buf) - 1)) {
            gd_prov_json_buf[gd_prov_json_len++] = ch;
        } else {
            gd_prov_json_len = 0;
            gd_prov_json_error_count++;
            rt_kprintf("[ble_prov] JSON too long\n");
            gd_prov_notify(conn_idx, "{\"status\":\"too_long\"}\n");
            continue;
        }

        if (submit_now) {
            gd_prov_json_buf[gd_prov_json_len] = '\0';
            gd_prov_submit_json(gd_prov_json_buf, conn_idx);
            gd_prov_json_len = 0;
            submit_now = false;
        }
    }
}

static void gd_prov_ble_conn_evt(uint8_t conn_idx, bool connected)
{
    if (connected) {
        gd_prov_ble_connected = 1;
        gd_prov_ble_conn_idx = conn_idx;
        rt_kprintf("[ble_prov] BLE connected, conn_idx=%d\n", conn_idx);
    } else {
        gd_prov_ble_connected = 0;
        gd_prov_ble_conn_idx = -1;
        gd_prov_json_len = 0;
        rt_kprintf("[ble_prov] BLE disconnected\n");
    }
}

static void gd_prov_worker(void *parameter)
{
    (void)parameter;

    gd_prov_store_credentials(GD_PROV_DEFAULT_SSID, GD_PROV_DEFAULT_PASSWORD);
    // rt_sem_release(gd_prov_sem);

    while (1) {
        char ssid[GD_PROV_MAX_SSID_LEN + 1];
        char password[GD_PROV_MAX_PASSWORD_LEN + 1];
        char response[64];
        char ip[16];
        uint8_t conn_idx;
        rt_base_t level;
        int ret;

        rt_sem_take(gd_prov_sem, RT_WAITING_FOREVER);

        level = rt_hw_interrupt_disable();
        rt_memcpy(ssid, gd_prov_ssid, sizeof(ssid));
        rt_memcpy(password, gd_prov_password, sizeof(password));
        conn_idx = (uint8_t)gd_prov_ble_conn_idx;
        rt_hw_interrupt_enable(level);

        gd_prov_wifi_connect_count++;
        gd_prov_notify(conn_idx, "{\"status\":\"connecting\"}\n");
        ret = gd_prov_connect_wifi(ssid, password);
        gd_prov_wifi_last_status = ret;

        if (ret == RT_EOK) {
            gd_prov_wait_ip_text(ip, sizeof(ip));
            rt_snprintf(response, sizeof(response),
                        "{\"status\":\"connected\",\"ip\":\"%s\"}\n", ip);
        } else {
            rt_snprintf(response, sizeof(response),
                        "{\"status\":\"failed\",\"code\":%d}\n", ret);
        }
        gd_prov_notify(conn_idx, response);
    }
}

static void gd_prov_main(void *parameter)
{
    rt_thread_t worker;

    (void)parameter;

    rt_kprintf("[ble_prov] main thread start\n");
    rt_thread_mdelay(500);

    gd_prov_sem = rt_sem_create("bleprov", 0, RT_IPC_FLAG_FIFO);
    if (gd_prov_sem == RT_NULL) {
        rt_kprintf("[ble_prov] create semaphore failed\n");
        return;
    }

    gd_prov_ble_init_ret = gd32vw55x_ble_stack_init(true);
    if (gd_prov_ble_init_ret != 0) {
        rt_kprintf("[ble_prov] BLE stack init failed: %d\n", gd_prov_ble_init_ret);
        return;
    }

    gd_prov_ble_datatrans_init_ret = gd32vw55x_ble_datatrans_init(gd_prov_ble_rx);
    if (gd_prov_ble_datatrans_init_ret != 0) {
        rt_kprintf("[ble_prov] BLE Datatrans init failed: %d\n", gd_prov_ble_datatrans_init_ret);
        return;
    }

    gd32vw55x_ble_datatrans_set_conn_cb(gd_prov_ble_conn_evt);
    gd_prov_ble_datatrans_init_ret = gd32vw55x_ble_datatrans_start(GD_PROV_BLE_NAME);
    if (gd_prov_ble_datatrans_init_ret != 0) {
        rt_kprintf("[ble_prov] BLE advertising start failed: %d\n", gd_prov_ble_datatrans_init_ret);
        return;
    }

    rt_kprintf("[ble_prov] BLE advertising as %s\n", GD_PROV_BLE_NAME);

    worker = rt_thread_create("bleprovw", gd_prov_worker, RT_NULL, 6144, 13, 20);
    if (worker == RT_NULL) {
        rt_kprintf("[ble_prov] create worker thread failed\n");
        return;
    }
    rt_thread_startup(worker);

    while (1) {
        rt_thread_mdelay(1000);
    }
}

static int gd_prov_start(void)
{
    rt_thread_t thread;

    rt_kprintf("[ble_prov] app init start\n");
    thread = rt_thread_create("bleprov", gd_prov_main, RT_NULL, 8192, 12, 20);
    if (thread == RT_NULL) {
        rt_kprintf("[ble_prov] create main thread failed\n");
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
    return RT_EOK;
}
INIT_APP_EXPORT(gd_prov_start);
