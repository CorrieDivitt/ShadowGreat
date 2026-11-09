# Shadowgate native build.
#
# Builds shadowgate.exe from YOUR OWN Shadowgate (USA) .nes file plus the
# feature code in src/. Nothing from the game is stored in this folder: the
# ROM is read at build time from wherever you keep it.
#
#   cmake -S . -B build-window -G "Visual Studio 17 2022" -A x64 ^
#         -DSHADOWGATE_ROM="F:/Shadowgreat/Shadowgate (USA).nes"
#   cmake --build build-window --config Release --parallel
#
# The narrator's voice is an optional "voice package" (sherpa-onnx DLLs +
# the Kokoro model) placed in a voice folder beside the .exe; nothing about
# it is needed at build time (config.ini VoiceFolder / VoiceModelFolder).
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
    src/sg_voice.c
    src/sg_menu.c
    src/sg_settings_link.c
    src/sg_hitboxes.c
    src/features/feat_mouse.c
    src/features/feat_hand.c
    src/features/feat_narration.c
    src/features/feat_torch.c
    src/features/feat_pad.c
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

# ---- the narrator's voice (sg_voice.c) ----
# The voice is an optional add-on: sherpa-onnx's DLLs and the Kokoro model go
# in a "voice" folder beside the .exe (the launcher downloads them on request).
# The game is always built able to use it: only sherpa-onnx's header is in the
# project (third_party/sherpa-onnx, Apache-2.0), and the DLL is loaded while
# the game runs. Without the voice package the game runs without narration.
# Our word list (voice/sg-names.txt) is copied beside the .exe.
if(NOT NESRECOMP_HEADLESS)
    target_include_directories(shadowgate PRIVATE "${CMAKE_SOURCE_DIR}/third_party/sherpa-onnx")
    target_compile_definitions(shadowgate PRIVATE SG_HAVE_VOICE)
    add_custom_command(TARGET shadowgate POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/voice/sg-names.txt" $<TARGET_FILE_DIR:shadowgate>)
    message(STATUS "shadowgate: voice support ON (uses the voice package if it is installed)")
endif()

# ---- in-game menu scaled with the window (patches/menu-scale/README.md) ----
# Only for builds with recomp-ui. Builds a patched copy of nesrecomp's
# cyc_ui_imgui.cpp in the build folder; nesrecomp's own file is untouched.
get_target_property(_sg_srcs shadowgate SOURCES)
set(_sg_ui_src "")
foreach(_s IN LISTS _sg_srcs)
    if(_s MATCHES "cyc_ui_imgui\\.cpp$")
        set(_sg_ui_src "${_s}")
    endif()
endforeach()
if(_sg_ui_src)
    set(_sg_p "${CMAKE_SOURCE_DIR}/patches/menu-scale")
    file(READ "${_sg_ui_src}" _sg_text)
    file(READ "${_sg_p}/helpers.inc" _sg_helpers)
    file(READ "${_sg_p}/init.inc" _sg_init)
    file(READ "${_sg_p}/newframe.inc" _sg_newframe)
    file(READ "${_sg_p}/render.inc" _sg_render)
    set(_sg_ok TRUE)
    foreach(_a
            "#include \"imgui_impl_sdlrenderer2.h\"\n"
            "    cyc_state_ui_init(ren, host);\n"
            "    ImGui_ImplSDL2_NewFrame();\n    ImGui::NewFrame();\n"
            "    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), s_ren);\n")
        string(FIND "${_sg_text}" "${_a}" _sg_at)
        if(_sg_at LESS 0)
            set(_sg_ok FALSE)
        endif()
    endforeach()
    if(_sg_ok)
        string(REPLACE "#include \"imgui_impl_sdlrenderer2.h\"\n"
                       "#include \"imgui_impl_sdlrenderer2.h\"\n${_sg_helpers}" _sg_text "${_sg_text}")
        string(REPLACE "    cyc_state_ui_init(ren, host);\n"
                       "    cyc_state_ui_init(ren, host);\n${_sg_init}" _sg_text "${_sg_text}")
        string(REPLACE "    ImGui_ImplSDL2_NewFrame();\n    ImGui::NewFrame();\n"
                       "    ImGui_ImplSDL2_NewFrame();\n${_sg_newframe}    ImGui::NewFrame();\n" _sg_text "${_sg_text}")
        string(REPLACE "    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), s_ren);\n"
                       "${_sg_render}" _sg_text "${_sg_text}")
        set(_sg_out "${CMAKE_BINARY_DIR}/generated/cyc_ui_imgui_scaled.cpp")
        file(WRITE "${_sg_out}.new" "${_sg_text}")
        file(COPY_FILE "${_sg_out}.new" "${_sg_out}" ONLY_IF_DIFFERENT)
        list(REMOVE_ITEM _sg_srcs "${_sg_ui_src}")
        list(APPEND _sg_srcs "${_sg_out}")
        set_property(TARGET shadowgate PROPERTY SOURCES ${_sg_srcs})
        get_filename_component(_sg_ui_dir "${_sg_ui_src}" DIRECTORY)
        target_include_directories(shadowgate PRIVATE "${_sg_ui_dir}")
        message(STATUS "shadowgate: in-game menu scales with the window")
    else()
        message(WARNING "shadowgate: nesrecomp's cyc_ui_imgui.cpp has changed; "
                        "the in-game menu keeps its normal (unscaled) size")
    endif()
endif()

# ---- the pad's B button is the NES B by default (patches/pad-b-button) ----
get_target_property(_sg_srcs shadowgate SOURCES)
set(_sg_in_src "")
foreach(_s IN LISTS _sg_srcs)
    if(_s MATCHES "cyc/cyc_input\\.c$")
        set(_sg_in_src "${_s}")
    endif()
endforeach()
if(_sg_in_src)
    file(READ "${_sg_in_src}" _sg_text)
    set(_sg_a "CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_A), CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_X),")
    string(FIND "${_sg_text}" "${_sg_a}" _sg_at)
    if(_sg_at GREATER_EQUAL 0)
        string(REPLACE "${_sg_a}"
               "CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_A), CYC_PAD_BUTTON(SDL_CONTROLLER_BUTTON_B),  /* Shadowgreat: B on B */"
               _sg_text "${_sg_text}")
        set(_sg_out "${CMAKE_BINARY_DIR}/generated/cyc_input_pad_b.c")
        file(WRITE "${_sg_out}.new" "${_sg_text}")
        file(COPY_FILE "${_sg_out}.new" "${_sg_out}" ONLY_IF_DIFFERENT)
        list(REMOVE_ITEM _sg_srcs "${_sg_in_src}")
        list(APPEND _sg_srcs "${_sg_out}")
        set_property(TARGET shadowgate PROPERTY SOURCES ${_sg_srcs})
        get_filename_component(_sg_in_dir "${_sg_in_src}" DIRECTORY)
        target_include_directories(shadowgate PRIVATE "${_sg_in_dir}")
        message(STATUS "shadowgate: the pad's B button is the NES B by default")
    else()
        message(WARNING "shadowgate: nesrecomp's cyc_input.c has changed; "
                        "the pad's X button stays the default NES B")
    endif()
endif()

# ---- "Restore Defaults" instead of OPEN on our reset row (patches/menu-action-label) ----
get_target_property(_sg_srcs shadowgate SOURCES)
set(_sg_rt_src "")
foreach(_s IN LISTS _sg_srcs)
    if(_s MATCHES "runtime_ui_imgui\\.cpp$")
        set(_sg_rt_src "${_s}")
    endif()
endforeach()
if(_sg_rt_src)
    file(READ "${_sg_rt_src}" _sg_text)
    file(READ "${CMAKE_SOURCE_DIR}/patches/menu-action-label/value.inc" _sg_value)
    set(_sg_a "        std::snprintf(out, out_size, \"OPEN\");\n")
    string(FIND "${_sg_text}" "${_sg_a}" _sg_at)
    if(_sg_at GREATER_EQUAL 0)
        string(REPLACE "${_sg_a}" "${_sg_value}" _sg_text "${_sg_text}")
        set(_sg_out "${CMAKE_BINARY_DIR}/generated/runtime_ui_imgui_labels.cpp")
        file(WRITE "${_sg_out}.new" "${_sg_text}")
        file(COPY_FILE "${_sg_out}.new" "${_sg_out}" ONLY_IF_DIFFERENT)
        list(REMOVE_ITEM _sg_srcs "${_sg_rt_src}")
        list(APPEND _sg_srcs "${_sg_out}")
        set_property(TARGET shadowgate PROPERTY SOURCES ${_sg_srcs})
        get_filename_component(_sg_rt_dir "${_sg_rt_src}" DIRECTORY)
        target_include_directories(shadowgate PRIVATE "${_sg_rt_dir}")
        message(STATUS "shadowgate: menu reset row says Restore Defaults")
    else()
        message(WARNING "shadowgate: recomp-ui's runtime_ui_imgui.cpp has changed; "
                        "the reset row shows OPEN")
    endif()
endif()

# ---- MinGW (llvm-mingw) builds: ship the compiler's C++ runtime beside the exe ----
# shadowgate.exe built with clang for MinGW needs libc++.dll and libunwind.dll.
# They are part of the compiler download (Apache-2.0 with LLVM exception), so
# the build copies them next to the exe; otherwise it only starts when the
# compiler folder happens to be on PATH.
if(MINGW)
    get_filename_component(_sg_cc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_sg_dll libc++.dll libunwind.dll)
        find_file(_sg_dll_path_${_sg_dll} ${_sg_dll}
            PATHS "${_sg_cc_bin}" "${_sg_cc_bin}/../x86_64-w64-mingw32/bin"
            NO_DEFAULT_PATH)
        if(_sg_dll_path_${_sg_dll})
            add_custom_command(TARGET shadowgate POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_sg_dll_path_${_sg_dll}}" $<TARGET_FILE_DIR:shadowgate>)
        else()
            message(WARNING "shadowgate: ${_sg_dll} not found next to the compiler; "
                            "copy it beside shadowgate.exe by hand")
        endif()
    endforeach()
endif()
