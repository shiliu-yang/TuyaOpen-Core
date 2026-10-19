file(GLOB_RECURSE _tal_system_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(tal_system STATIC)
target_sources(tal_system PRIVATE ${_tal_system_srcs})
target_include_directories(tal_system PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>

)
# tal_api.h is an umbrella header that includes sibling module headers (the
# esp-idf REQUIRES semantics), so propagation must be PUBLIC.
target_link_libraries(tal_system
    PUBLIC tuya_transport tuya_utilities
    PRIVATE tuyaopen-headers
)
unset(_tal_system_srcs)
