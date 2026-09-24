# 由原 core-common 重组而来，只保留通用/第三方库。
# - libtls 并入 tal_security 下（tal_asymmetrical 依赖它）
# - tal_network 并入 tuya_transport 下（联网抽象随传输层分组）
# - 原 common 的 utilities 并入 tal_system、backoffAlgorithm 并入 libmqtt
# - qrcode 移至应用层（examples/switch_demo/qrcode）
add_subdirectory(libcjson)
add_subdirectory(tuya_transport)
add_subdirectory(tuya_transport/tal_network)
add_subdirectory(libhttp)
add_subdirectory(libmqtt)
add_subdirectory(tal_system)
add_subdirectory(tal_kv)
add_subdirectory(tal_security)
add_subdirectory(tal_security/libtls)
