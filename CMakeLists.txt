# Shadowgate native build.
#
# Builds shadowgate.exe from YOUR OWN Shadowgate (USA) .nes file plus the
# feature code in src/. Nothing from the game is stored in this folder: the
# ROM is read at build time from wherever you keep it.
#
#   cmake -S . -B build-window -G "Visual Studio 17 2022" -A x64 ^
#         -DSHADOWGATE_ROM="F:/Shadowgreat/Shadowgate (USA).nes"
#   cmake --build build-window --config Release --parallel
cmake_minimum_required(VERSION 3.20)
project(Shadowgate C)

set(NESRECOMP_ROOT "F:/NESDev/nesrecomp" CACHE PATH "Your nesrecomp checkout")
set(SHADOWGATE_ROM "" CACHE FILEPATH "Your own Shadowgate (USA) .nes file")
option(NESRECOMP_HEADLESS "Build without a window (for tests)" OFF)

if(NOT SHADOWGATE_ROM)
    message(FATAL_ERROR "Tell CMake where your ROM is: -DSHADOWGATE_ROM=\"F:/path/Shadowgate (USA).nes\"")
endif()

include("${NESRECOMP_ROOT}/runner/cyc/project.cmake")

set(SHADOWGATE_FEATURES
    src/sg_host.c
    src/sg_game.c
    src/sg_text.c
    src/features/feat_mouse.c
    src/features/feat_narration.c
    src/features/feat_torch.c
)

set(_sg_options "")
if(NESRECOMP_HEADLESS)
    list(APPEND _sg_options HEADLESS)
endif()

nesrecomp_add_cycle_game(shadowgate
    ROM "${SHADOWGATE_ROM}"
    GAME_CONFIG "${CMAKE_SOURCE_DIR}/game.toml"
    HOST_EXTRAS ${SHADOWGATE_FEATURES}
    ${_sg_options})
target_include_directories(shadowgate PRIVATE "${CMAKE_SOURCE_DIR}/src"
    "${NESRECOMP_ROOT}/runner/include")
