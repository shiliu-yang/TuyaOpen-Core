# 通用工具层：平台无关的纯 C 工具（链表/队列/内存堆/ringbuf/slist/tools/crc/random/
# mix_method/dev_evt/queue_template）。由 tal_system PUBLIC 依赖，消费者自动可达。
#
# 注：本层与 tal_system / tuya_transport 互相引用头文件（mix_method→tal_memory.h、
# uni_random→tuya_tls.h；反向 tal_system/tuya_transport→tuya_list/mix_method.h）。
# 这是**头文件可见性需求而非符号依赖**：CMake 对静态库的 target 依赖环不报错
# （实测 exit 0），链接期符号由最终链接组（--start-group）解析。PRIVATE 依赖仅为
# 取头路径与传递官方库（mbedtls 等）的 include。
file(GLOB_RECURSE _utilities_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(tuya_utilities STATIC)
target_sources(tuya_utilities PRIVATE ${_utilities_srcs})
target_include_directories(tuya_utilities PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(tuya_utilities PRIVATE tuyaopen-headers tal_system tuya_transport)
unset(_utilities_srcs)
