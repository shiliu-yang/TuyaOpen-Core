# 仅作 esp-idf 组件入口：component manager 经 idf_component.yml 的 git 依赖
# 拉取本仓库时，从根目录注册组件。普通 CMake 宿主请 include(tuyaopen-core.cmake)，
# 不要使用本文件（idf_component_register 仅在 esp-idf 构建中可用）。
#
# REQUIRES mbedtls：esp-idf 宿主下 core 不注册自带 mbedtls（由平台提供，
# CONFIG_ENABLE_PLATFORM_MBEDTLS=ON），模块里对 mbedtls 目标的裸名引用由
# tuyaopen-core.cmake 桥接到 idf::mbedtls；此处 REQUIRES 保证 esp-idf 的
# mbedtls 组件先处理（组件处理顺序按 REQUIRES 拓扑展开）
idf_component_register(SRCS "" INCLUDE_DIRS "" REQUIRES mbedtls)

include(${CMAKE_CURRENT_LIST_DIR}/tuyaopen-core.cmake)

target_link_libraries(${COMPONENT_LIB} INTERFACE tuyaopen-core)
