/**
 * @file tuyaopen-core_ops.c
 * @brief tuyaopen-core 在老 tuyaopen 平台（tuyaos/tuya_open_sdk）上的 ops 卡注册：
 *        CORE_API_OPS_T / CORE_NETWORK_OPS_T / CORE_NETIF_OPS_T /
 *        CORE_BLUETOOTH_OPS_T / CORE_FS_OPS_T。
 *
 * 分层（C2）：core 的 core_api.c / core_network.c / core_netif.c / core_bluetooth.c
 * 经 extern 引用这些卡实例完成分发；卡成员直接绑定本平台的 tkl_* 实现（薄卡，无包装）。
 * 契约类型取 port/tuyaopen/interface/（core 与平台共用同一份定义，随 core 走）。
 *
 * 单一来源：本文件由三个平台（LINUX / T5AI / ESP32）的构建共同引用，平台不再各存
 * 一份拷贝；平台差异用条件编译收在文件内：
 *   - netif：ESP32 无 tkl_wired，绑定平台侧 netif_esp.c；其余平台绑定 tkl_wired_*，
 *     与平台 tkl_wired.c 同受 ENABLE_WIRED 门控；
 *   - BLE：成员绑定平台 tkl_ble_*（CORE_BLE_* 与 TKL_BLE_* 逐字段同布局，编译期
 *     sizeof/offsetof 断言兜底），ENABLE_BLUETOOTH 关闭时给全 NULL 空卡。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include <stddef.h>

#include "tuya_kconfig.h" /* generated switches: ENABLE_WIRED / ENABLE_BLUETOOTH / ENABLE_EXT_RAM */

#include "core_api_ops.h"       /* CORE_API_OPS_T */
#include "core_network_ops.h"   /* CORE_NETWORK_OPS_T */
#include "core_netif_ops.h"     /* CORE_NETIF_OPS_T */
#include "core_bluetooth_ops.h" /* CORE_BLUETOOTH_OPS_T */
#include "core_fs_ops.h"        /* CORE_FS_OPS_T */

#include "tkl_output.h"
#include "tkl_memory.h"
#include "tkl_system.h"
#include "tkl_semaphore.h"
#include "tkl_mutex.h"
#include "tkl_thread.h"
#include "tkl_queue.h"
#include "tkl_network.h"
#include "tkl_bluetooth.h"
#include "tkl_fs.h"

#if defined(ESP_PLATFORM)
#include "netif_esp.h" /* netif 卡实现（esp_netif 事件），见平台 tuyaopen/netif_esp.c */
#else
#include "tkl_wired.h"
#endif

/* ---------------------------------------------------------------------------
 * CORE_API_OPS_T：log / 内存 / system / 信号量 / 互斥 / 线程 / 队列
 * --------------------------------------------------------------------------- */
CORE_API_OPS_T core_api_ops_platform = {
    //! log output
    .log_output = tkl_log_output,
    //! memory
    .malloc = tkl_system_malloc,
    .free   = tkl_system_free,
    //! system
    .sleep           = tkl_system_sleep,
    .reset           = tkl_system_reset,
    .get_millisecond = tkl_system_get_millisecond,
    .get_random      = tkl_system_get_random,
    //! semaphore
    .semaphore_create_init = tkl_semaphore_create_init,
    .semaphore_wait        = tkl_semaphore_wait,
    .semaphore_post        = tkl_semaphore_post,
    .semaphore_release     = tkl_semaphore_release,
    //! mutex
    .mutex_create_init = tkl_mutex_create_init,
    .mutex_lock        = tkl_mutex_lock,
    .mutex_unlock      = tkl_mutex_unlock,
    .mutex_release     = tkl_mutex_release,
    //! thread
    .thread_create        = tkl_thread_create,
    .thread_release       = tkl_thread_release,
    .thread_set_self_name = tkl_thread_set_self_name,
    .thread_is_self       = tkl_thread_is_self,
    .thread_get_id        = tkl_thread_get_id,
    //! queue
    .queue_create_init = tkl_queue_create_init,
    .queue_post        = tkl_queue_post,
    .queue_fetch       = tkl_queue_fetch,
    .queue_free        = tkl_queue_free,
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    //! ex ram（外部 RAM，如 PSRAM）：复用平台既有 tkl_system_psram_*（Linux 无独立外部
    //! RAM，平台实现退化为普通内存）；宏隔离与 core 的 CORE_API_OPS_T 一致
    .ex_ram_malloc = tkl_system_psram_malloc,
    .ex_ram_free   = tkl_system_psram_free,
#endif
};

/* ---------------------------------------------------------------------------
 * CORE_NETWORK_OPS_T：标准网络卡（socket 全量绑定 tkl_net_*）；
 * 局域网/对端成员按 core 侧同名宏门控（core 的开关宏不传播到平台编译单元，
 * 故此处逐个判断）
 * --------------------------------------------------------------------------- */
CORE_NETWORK_OPS_T core_network_ops_platform = {
    .get_errno = tkl_net_get_errno,
    .fd_set = tkl_net_fd_set,
    .fd_isset = tkl_net_fd_isset,
    .fd_zero = tkl_net_fd_zero,
    .select = tkl_net_select,
    .get_nonblock = tkl_net_get_nonblock,
    .set_block = tkl_net_set_block,
    .close = tkl_net_close,
    .socket_create = tkl_net_socket_create,
    .connect = tkl_net_connect,
    .bind = tkl_net_bind,
    .send = tkl_net_send,
    .recv = tkl_net_recv,
    .set_timeout = tkl_net_set_timeout,
    .set_reuse = tkl_net_set_reuse,
    .disable_nagle = tkl_net_disable_nagle,
    .gethostbyname = tkl_net_gethostbyname,
    .set_keepalive = tkl_net_set_keepalive,
    .str2addr = tkl_net_str2addr,
    .addr2str = tkl_net_addr2str,
    .setsockopt = tkl_net_setsockopt,
#if defined(ENABLE_LOCAL_LINK) || defined(ENABLE_TUYA_P2P) || defined(ENABLE_AI_COMPONENTS)
    /* 局域网/对端服务成员 */
    .listen = tkl_net_listen,
    .accept = tkl_net_accept,
#endif
#if defined(ENABLE_LOCAL_LINK) || defined(ENABLE_TUYA_P2P)
    .send_to = tkl_net_send_to,
    .recvfrom = tkl_net_recvfrom,
#endif
#if defined(ENABLE_LOCAL_LINK)
    /* 局域网协议专用成员（tuya_lan.c 收帧） */
    .recv_nd_size = tkl_net_recv_nd_size,
#endif
#if defined(ENABLE_TUYA_P2P)
    /* p2p 专用成员 */
    .fd_clear = tkl_net_fd_clear,
    .shutdown = tkl_net_shutdown,
    .set_bufsize = tkl_net_set_bufsize,
    .getsockname = tkl_net_getsockname,
    .getpeername = tkl_net_getpeername,
#endif
};

/* ---------------------------------------------------------------------------
 * CORE_NETIF_OPS_T：链路状态 + 取 IP
 * - ESP32：平台无 tkl_wired，绑定平台侧 netif_esp.c（esp_netif/esp_wifi 事件）；
 * - 其余平台：复用 tkl_wired，受 ENABLE_WIRED 门控（与平台 tkl_wired.c 同开关；
 *   关掉 WIRED 时本卡不编，core 侧也不会引用它）。
 * 非 ESP32 分支的 set_link_cb 需薄包装——core 的
 * CORE_NETIF_LINK_CHANGE_CB(void(*)(CORE_NETIF_LINK_STAT_E)) 与 tkl 的
 * TKL_WIRED_STATUS_CHANGE_CB(void(*)(TKL_WIRED_STAT_E)) 是两个不同 C 类型
 * （值域一致 DOWN=0/UP=1），故存下 core 回调、向 tkl 注册转投 shim。
 * --------------------------------------------------------------------------- */
#if defined(ESP_PLATFORM)
CORE_NETIF_OPS_T core_netif_ops_platform = {
    .get_ip      = core_netif_esp_get_ip,
    .set_link_cb = core_netif_esp_set_link_cb,
};
#elif defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
static CORE_NETIF_LINK_CHANGE_CB s_link_cb;

static void __link_cb_shim(TKL_WIRED_STAT_E status)
{
    if (s_link_cb) {
        s_link_cb((CORE_NETIF_LINK_STAT_E)status);
    }
}

static OPERATE_RET __set_link_cb(CORE_NETIF_LINK_CHANGE_CB cb)
{
    s_link_cb = cb;
    return tkl_wired_set_status_cb(__link_cb_shim);
}

CORE_NETIF_OPS_T core_netif_ops_platform = {
    .get_ip      = tkl_wired_get_ip,
    .set_link_cb = __set_link_cb,
};
#endif /* ENABLE_WIRED */

/* ---------------------------------------------------------------------------
 * CORE_FS_OPS_T：文件系统卡，成员直接绑定平台 tkl_fs_* / tkl_f* / tkl_dir_*。
 * 新 core 的 core_fs.c 只做分发；实际介质（littlefs/裸 flash/POSIX）由平台 tkl_fs
 * 提供。core_fs.c 无被引用时静态库不会拉入该对象，本卡也就不会引入链接依赖。
 * --------------------------------------------------------------------------- */
CORE_FS_OPS_T core_fs_ops_platform = {
    /* file */
    .fopen   = tkl_fopen,
    .fclose  = tkl_fclose,
    .fwrite  = tkl_fwrite,
    .fread   = tkl_fread,
    .fseek   = tkl_fseek,
    .fflush  = tkl_fflush,
    .fsync   = tkl_fsync,
    .fileno  = tkl_fileno,
    .fgets   = tkl_fgets,
    /* path */
    .fs_is_exist = tkl_fs_is_exist,
    .fs_mkdir    = tkl_fs_mkdir,
    .fs_remove   = tkl_fs_remove,
    .fs_rename   = tkl_fs_rename,
    /* directory */
    .dir_open         = tkl_dir_open,
    .dir_close        = tkl_dir_close,
    .dir_read         = tkl_dir_read,
    .dir_name         = tkl_dir_name,
    .dir_is_directory = tkl_dir_is_directory,
};

/* ---------------------------------------------------------------------------
 * CORE_BLUETOOTH_OPS_T：BLE 卡（peripheral-only 18 成员），成员绑定平台 tkl_ble_*。
 *
 * 类型说明：core 卡成员用 CORE_BLE_* 镜像类型，平台 tkl 用 TKL_BLE_*；两者逐字段
 * 同布局（下面 sizeof/offsetof 断言兜底，镜像漂移即编译失败），故结构体指针与
 * 回调函数指针直接强转调用，不做逐字段拷贝。
 * ENABLE_BLUETOOTH 关闭时给全 NULL 空卡：core 侧 ble/ 不编、卡不被引用，定义了
 * 也不会引入 tkl_ble_* 的链接依赖。
 * --------------------------------------------------------------------------- */
#if defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1)

/* Compile-time layout asserts: a sizeof mismatch between a mirror type and its
 * tkl type fails the build (array length -1). */
#define CORE_BLE_LAYOUT_ASSERT(mirror, tkl) \
    typedef char _core_ble_assert_##mirror[(sizeof(mirror) == sizeof(tkl)) ? 1 : -1]

CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_ADDR_T, TKL_BLE_GAP_ADDR_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_DATA_T, TKL_BLE_DATA_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_ADV_PARAMS_T, TKL_BLE_GAP_ADV_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_ADV_REPORT_T, TKL_BLE_GAP_ADV_REPORT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_SCAN_PARAMS_T, TKL_BLE_GAP_SCAN_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_CONN_PARAMS_T, TKL_BLE_GAP_CONN_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_UUID_T, TKL_BLE_UUID_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_CHAR_PARAMS_T, TKL_BLE_CHAR_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_SERVICE_PARAMS_T, TKL_BLE_SERVICE_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATTS_PARAMS_T, TKL_BLE_GATTS_PARAMS_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_CONNECT_EVT_T, TKL_BLE_GAP_CONNECT_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_DISCONNECT_EVT_T, TKL_BLE_GAP_DISCONNECT_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_NOTIFY_RESULT_EVT_T, TKL_BLE_NOTIFY_RESULT_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_SVC_HANDLE_T, TKL_BLE_GATT_SVC_HANDLE_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_SVC_DISC_TYPE_T, TKL_BLE_GATT_SVC_DISC_TYPE_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_CHAR_HANDLE_T, TKL_BLE_GATT_CHAR_HANDLE_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_CHAR_DISC_TYPE_T, TKL_BLE_GATT_CHAR_DISC_TYPE_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_DESC_DISC_TYPE_T, TKL_BLE_GATT_DESC_DISC_TYPE_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_DATA_REPORT_T, TKL_BLE_DATA_REPORT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_SUBSCRIBE_EVT_T, TKL_BLE_SUBSCRBE_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_READ_CHAR_EVT_T, TKL_BLE_READ_CHAR_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T);
CORE_BLE_LAYOUT_ASSERT(CORE_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T);

/* Offset asserts for key event fields: a drifting union offset or header field
 * (conn_handle/result) would be read wrongly even when sizeof matches */
#define CORE_BLE_OFFSET_ASSERT(mirror, tkl, field) \
    typedef char _core_ble_off_assert_##mirror##_##field[(offsetof(mirror, field) == offsetof(tkl, field)) ? 1 : -1]

CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T, conn_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T, result);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_PARAMS_EVT_T, TKL_BLE_GAP_PARAMS_EVT_T, gap_event);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T, conn_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T, result);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_PARAMS_EVT_T, TKL_BLE_GATT_PARAMS_EVT_T, gatt_event);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_ADV_REPORT_T, TKL_BLE_GAP_ADV_REPORT_T, adv_type);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_ADV_REPORT_T, TKL_BLE_GAP_ADV_REPORT_T, peer_addr);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_ADV_REPORT_T, TKL_BLE_GAP_ADV_REPORT_T, rssi);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_ADV_REPORT_T, TKL_BLE_GAP_ADV_REPORT_T, data);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONNECT_EVT_T, TKL_BLE_GAP_CONNECT_EVT_T, role);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONNECT_EVT_T, TKL_BLE_GAP_CONNECT_EVT_T, peer_addr);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_DISCONNECT_EVT_T, TKL_BLE_GAP_DISCONNECT_EVT_T, reason);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_DISCONNECT_EVT_T, TKL_BLE_GAP_DISCONNECT_EVT_T, role);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONN_PARAMS_T, TKL_BLE_GAP_CONN_PARAMS_T, conn_interval_min);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONN_PARAMS_T, TKL_BLE_GAP_CONN_PARAMS_T, conn_interval_max);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONN_PARAMS_T, TKL_BLE_GAP_CONN_PARAMS_T, conn_latency);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GAP_CONN_PARAMS_T, TKL_BLE_GAP_CONN_PARAMS_T, conn_sup_timeout);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_NOTIFY_RESULT_EVT_T, TKL_BLE_NOTIFY_RESULT_EVT_T, char_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_NOTIFY_RESULT_EVT_T, TKL_BLE_NOTIFY_RESULT_EVT_T, result);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_DATA_REPORT_T, TKL_BLE_DATA_REPORT_T, char_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_DATA_REPORT_T, TKL_BLE_DATA_REPORT_T, report);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_SUBSCRIBE_EVT_T, TKL_BLE_SUBSCRBE_EVT_T, char_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_SUBSCRIBE_EVT_T, TKL_BLE_SUBSCRBE_EVT_T, reason);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_READ_CHAR_EVT_T, TKL_BLE_READ_CHAR_EVT_T, char_handle);
CORE_BLE_OFFSET_ASSERT(CORE_BLE_GATT_READ_CHAR_EVT_T, TKL_BLE_READ_CHAR_EVT_T, offset);

/* Callback registration wrappers: function-pointer casts (layout asserted above) */
static OPERATE_RET core_ble_ops_gap_callback_register(const CORE_BLE_GAP_EVT_FUNC_CB gap_evt)
{
    return tkl_ble_gap_callback_register((TKL_BLE_GAP_EVT_FUNC_CB)gap_evt);
}

static OPERATE_RET core_ble_ops_gatt_callback_register(const CORE_BLE_GATT_EVT_FUNC_CB gatt_evt)
{
    return tkl_ble_gatt_callback_register((TKL_BLE_GATT_EVT_FUNC_CB)gatt_evt);
}

/* Struct-argument wrappers: identical mirror layout, so pointers are cast */
static OPERATE_RET core_ble_ops_gap_addr_set(CORE_BLE_GAP_ADDR_T const *p_peer_addr)
{
    return tkl_ble_gap_addr_set((TKL_BLE_GAP_ADDR_T const *)p_peer_addr);
}

static OPERATE_RET core_ble_ops_gap_address_get(CORE_BLE_GAP_ADDR_T *p_peer_addr)
{
    return tkl_ble_gap_address_get((TKL_BLE_GAP_ADDR_T *)p_peer_addr);
}

static OPERATE_RET core_ble_ops_gap_adv_start(CORE_BLE_GAP_ADV_PARAMS_T const *p_adv_params)
{
    return tkl_ble_gap_adv_start((TKL_BLE_GAP_ADV_PARAMS_T const *)p_adv_params);
}

static OPERATE_RET core_ble_ops_gap_adv_rsp_data_set(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp)
{
    return tkl_ble_gap_adv_rsp_data_set((TKL_BLE_DATA_T const *)p_adv, (TKL_BLE_DATA_T const *)p_scan_rsp);
}

static OPERATE_RET core_ble_ops_gap_adv_rsp_data_update(CORE_BLE_DATA_T const *p_adv, CORE_BLE_DATA_T const *p_scan_rsp)
{
    return tkl_ble_gap_adv_rsp_data_update((TKL_BLE_DATA_T const *)p_adv, (TKL_BLE_DATA_T const *)p_scan_rsp);
}

static OPERATE_RET core_ble_ops_gap_conn_param_update(uint16_t conn_handle,
                                                      CORE_BLE_GAP_CONN_PARAMS_T const *p_conn_params)
{
    return tkl_ble_gap_conn_param_update(conn_handle, (TKL_BLE_GAP_CONN_PARAMS_T const *)p_conn_params);
}

/* non-const: the implementation writes service/char handles back, the cast keeps that possible */
static OPERATE_RET core_ble_ops_gatts_service_add(CORE_BLE_GATTS_PARAMS_T *p_service)
{
    return tkl_ble_gatts_service_add((TKL_BLE_GATTS_PARAMS_T *)p_service);
}

/* Peripheral-only card: every remaining member is bound. */
CORE_BLUETOOTH_OPS_T core_bluetooth_ops_platform = {
    /* stack */
    .stack_init               = tkl_ble_stack_init,
    .stack_deinit             = tkl_ble_stack_deinit,
    .stack_gatt_link          = tkl_ble_stack_gatt_link,
    /* event registration (must precede stack_init) */
    .gap_callback_register    = core_ble_ops_gap_callback_register,
    .gatt_callback_register   = core_ble_ops_gatt_callback_register,
    /* GAP */
    .gap_addr_set             = core_ble_ops_gap_addr_set,
    .gap_address_get           = core_ble_ops_gap_address_get,
    .gap_adv_start            = core_ble_ops_gap_adv_start,
    .gap_adv_stop             = tkl_ble_gap_adv_stop,
    .gap_adv_rsp_data_set     = core_ble_ops_gap_adv_rsp_data_set,
    .gap_adv_rsp_data_update  = core_ble_ops_gap_adv_rsp_data_update,
    .gap_disconnect           = tkl_ble_gap_disconnect,
    .gap_conn_param_update    = core_ble_ops_gap_conn_param_update,
    .gap_rssi_get             = tkl_ble_gap_rssi_get,
    /* GATT server */
    .gatts_service_add        = core_ble_ops_gatts_service_add,
    .gatts_value_set          = tkl_ble_gatts_value_set,
    .gatts_value_notify       = tkl_ble_gatts_value_notify,
    .gatts_exchange_mtu_reply = tkl_ble_gatts_exchange_mtu_reply,
};

#else /* !defined(ENABLE_BLUETOOTH) || (ENABLE_BLUETOOTH == 0) */

/* Fallback card: every member is NULL, core's dispatch degrades to OPRT_NOT_SUPPORTED. */
CORE_BLUETOOTH_OPS_T core_bluetooth_ops_platform = { 0 };

#endif /* defined(ENABLE_BLUETOOTH) && (ENABLE_BLUETOOTH == 1) */
