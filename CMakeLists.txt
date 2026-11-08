# Shadowgate native build.
#
# Builds shadowgate.exe from YOUR OWN Shadowgate (USA) .nes file plus the
# feature code in src/. Nothing from the game is stored in this folder: the
# ROM is read at build time from wherever you keep it.
#
#   cmake -S . -B build-window -G "Visual Studio 17 2022" -A x64 ^
#         -DSHADOWGATE_ROM="F:/Shadowgreat/Shadowgate (USA).nes"
#   cmake --build build-window --config Release --parallel
cmake_minimum_required(VERSION 3.21)
project(Shadowgate C)

set(NESRECOMP_ROOT "F:/NESDev/nesrecomp" CACHE PATH "Your nesrecomp checkout")
set(SHADOWGATE_ROM "" CACHE FILEPATH "Your own Shadowgate (USA) .nes file")
option(NESRECOMP_HEADLESS "Build without a window (for tests)" OFF)

if(NOT SHADOWGATE_ROM)
    message(FATAL_ERROR "Tell CMake where your ROM is: -DSHADOWGATE_ROM=\"F:/path/Shadowgate (USA).nes\"")
endif()

include("${NESRECOMP_ROOT}/runner/cyc/project.cmake")

# The mouse hand cursor (feat_hand.c) is drawn with the game's own hand
# tiles, read here from YOUR ROM into the build folder (never the project).
# Where they are is recorded in shadowgate-map.toml [hand_cursor].
set(SG_HAND_CHR_OFFSET 0x17A0)   # first of the 4 tiles, inside CHR-ROM
file(READ "${SHADOWGATE_ROM}" _sg_prg_banks OFFSET 4 LIMIT 1 HEX)
math(EXPR _sg_hand_offset "16 + 0x${_sg_prg_banks} * 16384 + ${SG_HAND_CHR_OFFSET}")
file(READ "${SHADOWGATE_ROM}" _sg_hand_hex OFFSET ${_sg_hand_offset} LIMIT 64 HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _sg_hand_bytes "${_sg_hand_hex}")
set(_sg_gen "${CMAKE_BINARY_DIR}/generated")
file(WRITE "${_sg_gen}/sg_hand_tiles.h.new"
    "/* GENERATED at build time from your ROM: the hand cursor's 4 tiles. */\n"
    "#define SG_HAND_CHR_BYTES { ${_sg_hand_bytes} }\n")
file(COPY_FILE "${_sg_gen}/sg_hand_tiles.h.new" "${_sg_gen}/sg_hand_tiles.h" ONLY_IF_DIFFERENT)

set(SHADOWGATE_FEATURES
    src/sg_host.c
    src/sg_game.c
    src/sg_text.c
    src/sg_hitboxes.c
    src/features/feat_mouse.c
    src/features/feat_hand.c
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
    "${CMAKE_BINARY_DIR}/generated"
    "${NESRECOMP_ROOT}/runner/include")
