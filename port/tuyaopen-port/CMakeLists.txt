# esp-idf component entry for the ESP32 port.
#
# esp-idf derives a component's name from its directory name, so the
# platform-named implementation directory (port/esp-idf) cannot itself be the
# component `tuyaopen-port`. This thin entry keeps that layout: it runs in the
# component context of this directory and includes the implementation build
# file, which registers the component and builds the sources/headers from
# port/esp-idf. The application adds this directory with EXTRA_COMPONENT_DIRS
# and depends on `tuyaopen-port` like any other component.
include(${CMAKE_CURRENT_LIST_DIR}/../esp-idf/CMakeLists.txt)
