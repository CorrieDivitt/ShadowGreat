# The Shadowgreat launcher: picks and checks the player's ROM, builds the
# game on this PC, and starts it. It contains no game data.
#
# Build (from the tools window, in the ShadowgatePort folder):
#   cmake -S launcher -B build-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release
#         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
#         -DSDL2_DIR=<SDL2>/x86_64-w64-mingw32/lib/cmake/SDL2
#   cmake --build build-launcher
cmake_minimum_required(VERSION 3.16)
project(ShadowgreatLauncher C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dear ImGui (MIT). Not copied into this project: we use recomp-ui's copy.
set(RECOMP_UI_DIR "F:/NESDev/recomp-ui" CACHE PATH "recomp-ui folder (for Dear ImGui and the Lato font)")
set(IMGUI_DIR "${RECOMP_UI_DIR}/src/third_party/imgui" CACHE PATH "Dear ImGui folder")
if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
  message(FATAL_ERROR "Dear ImGui not found in ${IMGUI_DIR}. Set -DRECOMP_UI_DIR=<your recomp-ui folder>.")
endif()

find_package(SDL2 REQUIRED CONFIG)

add_executable(ShadowgreatLauncher WIN32
  src/main.cpp
  src/launcher_settings.cpp
  src/build_job.cpp
  src/game_config.cpp
  src/paths.cpp
  src/child_process.cpp
  src/rom_check.c
  src/sha1.c
  src/sha256.c
  src/inflate.c
  src/untar.cpp
  src/http_download.cpp
  src/utf8_file.c
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(ShadowgreatLauncher PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(ShadowgreatLauncher PRIVATE SDL2::SDL2main SDL2::SDL2)
if(WIN32)
  target_link_libraries(ShadowgreatLauncher PRIVATE comdlg32 winhttp)
  # Name, version and manifest (launcher.rc).
  enable_language(RC)
  target_sources(ShadowgreatLauncher PRIVATE launcher.rc)
  set_source_files_properties(launcher.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/launcher.manifest)
endif()

# Everything the launcher needs goes beside it.
add_custom_command(TARGET ShadowgreatLauncher POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:ShadowgreatLauncher>
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ShadowgreatLauncher>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/fetch.py $<TARGET_FILE_DIR:ShadowgreatLauncher>)
file(GLOB_RECURSE _lato "${RECOMP_UI_DIR}/assets/LatoLatin-Regular.ttf")
if(_lato)
  list(GET _lato 0 _lato)
  add_custom_command(TARGET ShadowgreatLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_lato}" $<TARGET_FILE_DIR:ShadowgreatLauncher>/assets/)
else()
  message(WARNING "Lato font not found under ${RECOMP_UI_DIR}/assets; the launcher will use ImGui's built-in font.")
endif()
if(MINGW)
  get_filename_component(_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  foreach(_dll libc++.dll libunwind.dll)
    find_file(_p_${_dll} ${_dll} HINTS "${_bin}" "${_bin}/../x86_64-w64-mingw32/bin" NO_DEFAULT_PATH)
    if(_p_${_dll})
      add_custom_command(TARGET ShadowgreatLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_p_${_dll}}" $<TARGET_FILE_DIR:ShadowgreatLauncher>)
    endif()
  endforeach()
endif()
