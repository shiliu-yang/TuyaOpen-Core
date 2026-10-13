# AI 服务层（tuya_ai_service：AI 协议/传输、编解码注册表、MCP server 核心）。
# 应用侧组件（utility/audio_player/ai_audio/ai_agent/ai_skills/ai_mcp 工具集）
# 已下沉应用层，由应用自行组织（见 examples/agent_demo/components/）。
if(NOT CONFIG_ENABLE_AI_COMPONENTS)
    return()
endif()

add_subdirectory(tuya_ai_service)
