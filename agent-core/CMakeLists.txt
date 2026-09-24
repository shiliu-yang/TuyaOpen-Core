# tuya_ai_service 的 port 层（mqc_app.c）依赖 netmgr API；netmgr 组件已从
# iot-core（原 core-iot）移除，故 service 与组件层一并受 CONFIG_ENABLE_AI_COMPONENTS 门控。
# 重新启用 linux AI 需先为 mqc_app.c 提供脱离 netmgr 的 linkage/IP 实现。
if(NOT CONFIG_ENABLE_AI_COMPONENTS)
    return()
endif()

add_subdirectory(tuya_ai_service)
add_subdirectory(utility)
add_subdirectory(ai_agent)
add_subdirectory(ai_main)
add_subdirectory(ai_skills)
add_subdirectory(ai_audio)
add_subdirectory(ai_mcp)
add_subdirectory(ai_video)
