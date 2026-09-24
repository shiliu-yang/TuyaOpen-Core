# tal_wifi / tal_wifi_ulp 仅在启用 WIFI 时构建；无 WIFI 构建下
# tkl_wifi 实现由 port 侧剔除，TAL 包装层一并不编
if(CONFIG_ENABLE_WIFI)
    add_subdirectory(tal_wifi)
    add_subdirectory(tal_wifi_ulp)
endif()
add_subdirectory(tal_wired)
add_subdirectory(tal_bluetooth)
add_subdirectory(tal_cellular)
