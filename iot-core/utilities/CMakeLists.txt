# 通用工具层：平台无关的纯 C 工具（链表/队列/内存堆/ringbuf/slist/tools/crc/random/
# mix_method/dev_evt/queue_template）。由 tal_system PUBLIC 依赖，消费者自动可达。
#
# 注：本层与 tal_system / tuya_transport 互相引用头（mix_method→tal_memory.h、
# uni_random→tuya_tls.h；反向 tal_system/tuya_transport→tuya_list/mix_method.h），
# 建立 target 依赖会成环；故编译期依赖用显式 include 路径（C 的扁平 include 下
# 等价），链接期符号由最终链接组解析。
file(GLOB_RECURSE _utilities_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(tuya_utilities STATIC)
target_sources(tuya_utilities PRIVATE ${_utilities_srcs})
target_include_directories(tuya_utilities PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_include_directories(tuya_utilities PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tal_system/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../tuya_transport/include/tls
    ${CMAKE_CURRENT_SOURCE_DIR}/../tuya_transport/config
)
target_link_libraries(tuya_utilities PRIVATE tuyaopen-headers)
if(TARGET mbedtls)
    target_link_libraries(tuya_utilities PRIVATE mbedtls)
endif()
unset(_utilities_srcs)
