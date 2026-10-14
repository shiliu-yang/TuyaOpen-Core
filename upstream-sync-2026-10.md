# 上游同步记录：tuya_ai_service / tuya_cloud_service（2026-10-14）

**范围**：上游 tuyaopen（`/home/share/samba/open-components/tuyaopen`）的 `src/tuya_ai_service/`
与 `src/tuya_cloud_service/` 近期更新盘点。本仓 `linux/tuyaopen-core/` 为上游裁剪移植版
（tal_* 化、svc_ai_codec 已删、AI 组件位于应用层 `examples/agent_demo/src/components/`）。
本文档记录本轮逐项评估与处置（合并 / 未合并及原因）。

## 一、已合并

### 1. BLE 安全修复（上游 `ec8aa7cb`，2026-10-13）—— 全量移植

**上游问题**：BLE 接收路径的缓冲尺寸与拷贝长度取自 peer 提供的字段且未做校验——未认证
peer 在广播范围内用单个 GATT write 即可触发内存破坏 / NULL 解引用 / 未认证恢复出厂。

**处置**：7 文件 21 处修复点逐行移植（与上游保序逐行 diff IDENTICAL）：

| 文件 | 处数 | 要点 |
|---|---|---|
| `iot-core/ble/ble_protocol.h` | 1 | 3 个边界宏：TOTAL_LEN_MAX / SUBPACK_HEADER_MAX / SUBPACK_LEN_MIN |
| `iot-core/ble/ble_channel.c` | 5 | varint 重组边界；入口短帧检查；首/后续 subpacket 长度与偏移校验 |
| `iot-core/ble/ble_trsmitr.c` | 4 | 分包下限；varint 循环边界；version/seq 读取前检查；recv 上限 |
| `iot-core/ble/ble_dp.c` | 6 | 命令最小长度；DT_BOOL/BITMAP/VALUE/ENUM 的 NULL 与越界检查 |
| `iot-core/ble/ble_mgr.c` | 5 | 已绑定设备拒绝明文帧；unbind 需已配对；dev_info 长度校验；先分配后释放 |
| `iot-core/ble/ble_netcfg.c` | 1 | 响应缓冲显式初始化 |
| `iot-core/schema/dp_schema.c` | 1 | `dp_node_find` NULL schema 检查（激活前 BLE DP 路径可达） |

**验证**：蓝牙开启构建 rc=0（384 编译单元、7 个 `ble/*.c` 确认编译、本次修改文件 0 告警）；
ASAN 探针 + 5 组变异交叉验证（变异全红且定位到对应修复行、基线全绿、各判据互不误伤）。

**注意**：`linux/.config` 默认 `CONFIG_ENABLE_BLUETOOTH` 未开——验证需 `-DCONFIG_ENABLE_BLUETOOTH=ON`。

### 2. AI media header 长度校验（上游 `9df74f64`，2026-10-09）—— 全量移植

**上游问题**：`__ai_parse_biz_head` 对 VIDEO/AUDIO/IMAGE 直接裸指针强转解析，畸形/截断包可越界。

**处置**：`agent-core/svc_ai_basic/src/tuya_ai_biz.c` 的同函数顶部加媒体头长度预计算 +
`payload_len < media_head_len` 拒绝（+27 行，零适配）。本仓同函数同缺校验；其 FILE/TEXT
段本仓已有同款检查，本次补齐对称保护。

### 3. HTTP CA use-after-free（上游 `b021da62` 的 (a) 部分，2026-09-28）—— 适配移植

**上游问题**：`http_session_send` 在 `TUYA_TRANSPORTER_SET_TLS_CONFIG` 后立即 free cacert，
但 TLS 层只 memcpy config 结构体、不拷贝证书数据，mbedtls 解析在
`tuya_transporter_connect()` 内才执行——已释放内存被 connect 内部复用 → mbedtls 读到垃圾
（`MBEDTLS_ERR_X509_INVALID_FORMAT`），TLS 连接随机失败（US 区 CDN 必现、CN 区偶发）。

**处置**：`agent-core/port/src/http_manager.c`——cacert 声明外提、free 移到 connect 之后
（成败均释放）、SET_TLS_CONFIG 失败分支补 free。**两处本地适配（有意偏离上游）**：

- 错误分支**未采纳**上游的 `http_safe_free(host/path)`——本仓已收敛到
  `reset_response()` 统一释放，照抄会 double-free 回归（本仓既有评审结论）；
- **未采纳**上游"空 CA 硬失败"——本仓 `tuya_tls.c` 对 NULL CA 回退内置 CA
  （legacy `mbedtls_cacert_load`），保留该兜底（如需收紧另案评审）。

### 4. 语言无关提示音（上游 `5e9b27b4` 的 tone 部分，2026-09-23）—— 移植（应用层）

**上游方案**：4 个语义 tone（success/wait/fail/wake，MP3 C 数组 ~14KB）替代本地化语音包
（~2MB）；13 种 LOCAL_ALERT 类型映射到 4 tone。

**本仓背景**：`examples/agent_demo/src/components/ai_audio/ai_audio_player.c` 的
`__player_local_alert` 原为"素材未移植、降级 warning"的空壳（Kconfig 自述 Phase 2 待补）。

**处置**：新增 `local_alert_src_tone.c`（数据自上游逐字节提取，diff IDENTICAL）+ 空壳换为
完整映射实现（13 类型 → 4 tone + `ai_audio_play_data(AI_AUDIO_CODEC_MP3, ...)`）。
本仓无 assets 层与语言包，未引入 `ENABLE_AI_ALERT_TONE` 开关（tone 即唯一 LOCAL 来源）。

## 二、未合并（及原因）

| 上游提交 / 部分 | 原因 |
|---|---|
| `436c2af0` `__ai_crt_ringbuf` NULL guard | **本仓已含**：`agent-core/svc_ai_agent/src/tuya_ai_input.c` 的 guard 与上游 post-fix 逐字相同（调用方也已按 NULL 返回值处理） |
| `b021da62` (b)(c)：`http_session_read_content` handler + `tuya_ai_http.c` 适配 | 本仓媒体下载路径为**显式安全失败**（`tuya_ai_http.c` "not implemented, abort"，见 agent_demo README 已知限制）——handler 无消费者；待实现流式下载时作为前置件一并引入 |
| `5e9b27b4` (b)：guided WeChat UI 的 license 缓存 | 本仓无 `ai_ui` / `guided_wechat` 组件 |
| `5e9b27b4` (c)：SoftAP 热点名提示（`ap_netcfg_get_hotspot_info`） | 本仓无配网二维码 UI 与 ap_netcfg |
| `61e6c645`（esp32c2 TLS memory）/ `24104ca9`（TLS entropy hardware poll）等 | 平台专有（esp32c2 / 硬件熵源）——不在本仓（linux port）适用范围 |
| 其余上游提交（板级支持 / 显示驱动 / p2p / 4G 等） | 不在本次盘点范围（`tuya_ai_service` / `tuya_cloud_service` 之外） |

## 三、附注

- **盘点方法**：`git log -- src/tuya_ai_service src/tuya_cloud_service` 近期提交 + 逐提交
  `git show` 对照本仓对应文件；每条合并项均经构建与（安全类）变异验证。
- **与上游一致的有意保留项**（未擅改）：`ble_channel.c` 首 subpacket 长度校验失败时已分配
  的 static `pBuffer` 未即时释放（下次首包先 free，不会增长）；上游声明的 latent finding 一处。
- 本记录对应的提交见仓库 log（"上游同步"系列）。
