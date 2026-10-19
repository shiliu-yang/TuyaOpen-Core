# esp-idf component entry only: when the component manager pulls this repository
# through the git dependency in idf_component.yml, the component is registered
# from the repository root. Plain CMake hosts should include(tuyaopen-core.cmake)
# and must not use this file (idf_component_register is only available in
# esp-idf builds).
idf_component_register(SRCS "" INCLUDE_DIRS "")

include(${CMAKE_CURRENT_LIST_DIR}/tuyaopen-core.cmake)

target_link_libraries(${COMPONENT_LIB} INTERFACE tuyaopen-core)
