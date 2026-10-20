# esp-idf component entry only: when the component manager pulls this repository
# through the git dependency in idf_component.yml, the component is registered
# from the repository root. Plain CMake hosts should include(tuyaopen-core.cmake)
# and must not use this file (idf_component_register is only available in
# esp-idf builds).
#
# REQUIRES mbedtls: the core modules link the bare name `mbedtls` in
# target_link_libraries; as an esp-idf component this must resolve to esp-idf's
# mbedtls component target, which requires that component to be processed first
# (an explicit dependency edge -- alphabetical processing order is not
# guaranteed). This root file is only used by esp-idf builds; host CMake builds
# include tuyaopen-core.cmake directly and are unaffected.
idf_component_register(SRCS "" INCLUDE_DIRS "" REQUIRES mbedtls)

include(${CMAKE_CURRENT_LIST_DIR}/tuyaopen-core.cmake)

target_link_libraries(${COMPONENT_LIB} INTERFACE tuyaopen-core)
