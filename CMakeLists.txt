cmake_minimum_required(VERSION 3.28.0)

project(launcher-openvr-overlay)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)
include(GNUInstallDirs)
find_package(PkgConfig)

pkg_check_modules(PKG REQUIRED giomm-2.68 openvr glew x11 tbb)

include_directories(
  before
  src
)

include_directories(
  AFTER SYSTEM
  ${SDL2_INCLUDE_DIRS}
  ${PKG_INCLUDE_DIRS}
  deps/libnkutils/core/include
  ${CMAKE_BINARY_DIR}/imgui
)

add_compile_definitions(NK_EXPORT=extern "C")
add_compile_definitions(SYSCONFDIR="${CMAKE_INSTALL_FULL_SYSCONFDIR}")
add_compile_definitions(DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}")

add_library(
  nkutils STATIC
  deps/libnkutils/core/src/xdg-theme.c
  deps/libnkutils/core/src/enum.c
  deps/libnkutils/core/src/xdg-de.c
  deps/libnkutils/core/src/gtk-settings.c
)

set(IMGUI_SRC
  deps/imgui/imgui.cpp
  deps/imgui/imgui_tables.cpp
  deps/imgui/imgui_widgets.cpp
  deps/imgui/imgui_demo.cpp
  deps/imgui/imgui_draw.cpp

  deps/imgui/backends/imgui_impl_opengl3.cpp

  deps/imgui/imgui.h
  deps/imgui/imgui_internal.h

  deps/imgui/imstb_rectpack.h
  deps/imgui/imstb_textedit.h
  deps/imgui/imstb_truetype.h

  deps/imgui/backends/imgui_impl_opengl3.h
  deps/imgui/backends/imgui_impl_opengl3_loader.h
)

foreach(SRC ${IMGUI_SRC})
  get_filename_component(FILENAME ${SRC} NAME)
  configure_file(${SRC} imgui/${FILENAME})
endforeach()

add_library(
  imgui STATIC
  ${CMAKE_BINARY_DIR}/imgui/imgui.cpp
  ${CMAKE_BINARY_DIR}/imgui/imgui_tables.cpp
  ${CMAKE_BINARY_DIR}/imgui/imgui_widgets.cpp
  ${CMAKE_BINARY_DIR}/imgui/imgui_demo.cpp
  ${CMAKE_BINARY_DIR}/imgui/imgui_draw.cpp
  ${CMAKE_BINARY_DIR}/imgui/imgui_impl_opengl3.cpp
)

add_executable(launcher-openvr-overlay src/main.cpp)
target_link_libraries(
  launcher-openvr-overlay
  ${SDL2_LIBRARIES}
  ${PKG_LIBRARIES}
  nkutils
  imgui)

install(TARGETS launcher-openvr-overlay DESTINATION bin)

install(FILES icons/launcher-openvr-overlay.png DESTINATION
  ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
install(FILES icons/launcher-openvr-overlay.svg DESTINATION
  ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

install(FILES icons/launcher-openvr-overlay.png DESTINATION
  ${CMAKE_INSTALL_DATADIR}/pixmaps)
install(FILES icons/launcher-openvr-overlay.svg DESTINATION
  ${CMAKE_INSTALL_DATADIR}/pixmaps)

configure_file(manifest.vrmanifest.in manifest.vrmanifest)

install(FILES ${CMAKE_BINARY_DIR}/manifest.vrmanifest DESTINATION
  ${CMAKE_INSTALL_DATADIR}/launcher-openvr-overlay)
