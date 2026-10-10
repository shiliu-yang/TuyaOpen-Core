# 仅作 esp-idf 组件入口：component manager 经 idf_component.yml 的 git 依赖
# 拉取本仓库时，从根目录注册组件。普通 CMake 宿主请 include(tuyaopen-core.cmake)，
# 不要使用本文件（idf_component_register 仅在 esp-idf 构建中可用）。
idf_component_register(SRCS "" INCLUDE_DIRS "")

include(${CMAKE_CURRENT_LIST_DIR}/tuyaopen-core.cmake)

target_link_libraries(${COMPONENT_LIB} INTERFACE tuyaopen-core)
