# AI 服务层（tuya_ai_service target：AI 协议/传输、编解码注册表、MCP server 核心）。
# 应用侧组件（utility/audio_player/ai_audio/ai_agent/ai_skills/ai_mcp 工具集）
# 已下沉应用层，由应用自行组织（见 examples/agent_demo/components/）。
# 2026-10-13 目录扁平化：原 tuya_ai_service/ 子目录层去除，svc_*/port 提升至本层，
# 本文件即原 tuya_ai_service/CMakeLists.txt（CMAKE_CURRENT_SOURCE_DIR 相对路径不变）。
if(NOT CONFIG_ENABLE_AI_COMPONENTS)
    return()
endif()

set(_ai_srcs)
set(_ai_incs)

list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/port/include)
file(GLOB_RECURSE _ai_port_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/port/src/*.c")
list(APPEND _ai_srcs ${_ai_port_srcs})
unset(_ai_port_srcs)

file(GLOB_RECURSE _ai_agent_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_agent/src/*.c")
list(APPEND _ai_srcs ${_ai_agent_srcs})
list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_agent/include)
unset(_ai_agent_srcs)

file(GLOB_RECURSE _ai_basic_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_basic/src/*.c")
list(APPEND _ai_srcs ${_ai_basic_srcs})
list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_basic/include)
unset(_ai_basic_srcs)

list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/include)
if(CONFIG_ENABLE_TUYA_CODEC)
    list(APPEND _ai_srcs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/src/tuya_ai_encoder.c)
    if(CONFIG_ENABLE_TUYA_CODEC_OPUS)
        list(APPEND _ai_srcs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/src/tuya_ai_encoder_opus.c)
    endif()
    if(CONFIG_ENABLE_TUYA_CODEC_OPUS_IPC)
        list(APPEND _ai_srcs
            ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/src/tuya_ai_encoder_opus_ipc.c
            ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/src/tuya_ai_encoder_opus_ipc_server.c
        )
    endif()
    if(CONFIG_ENABLE_TUYA_CODEC_SPEEX)
        list(APPEND _ai_srcs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/src/tuya_ai_encoder_speex.c)
    endif()
endif()

file(GLOB_RECURSE _ai_monitor_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_monitor/src/*.c")
list(APPEND _ai_srcs ${_ai_monitor_srcs})
list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_monitor/include)
unset(_ai_monitor_srcs)

# MCP server 核心（协议解析 + 与 svc_ai_agent 的 mcp_cb 接线）。工具集
# （ai_mcp）在应用层，经本 include 路径使用其公开 API；依赖零新增
# （mix_method/uni_base64/cJSON 均已在既有依赖面内）。开关随模块
# （svc_ai_mcp/Kconfig 定义 ENABLE_COMP_AI_MCP，上游语义即门控 MCP 模块）。
if(CONFIG_ENABLE_COMP_AI_MCP)
    file(GLOB_RECURSE _ai_mcp_server_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_mcp/src/*.c")
    list(APPEND _ai_srcs ${_ai_mcp_server_srcs})
    list(APPEND _ai_incs ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_mcp/include)
    unset(_ai_mcp_server_srcs)
endif()

add_library(tuya_ai_service STATIC)
target_sources(tuya_ai_service PRIVATE ${_ai_srcs})
target_include_directories(tuya_ai_service PUBLIC ${_ai_incs})
target_include_directories(tuya_ai_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/svc_ai_codec/include
)
target_link_libraries(tuya_ai_service PRIVATE
    tuyaopen-headers
    libcjson
    tuya_transport
    tal_system
    tuya_cloud_service
)
unset(_ai_srcs)
unset(_ai_incs)
