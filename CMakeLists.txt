cmake_minimum_required(VERSION 3.16)
project(Impera C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(IMPERA_BUNDLED_SDL "Build pinned SDL dependencies for portable releases" OFF)
if(IMPERA_BUNDLED_SDL)
    include(cmake/ReleaseDependencies.cmake)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED sdl3)
    pkg_check_modules(SDL3_MIXER REQUIRED sdl3-mixer)
endif()

set(SOURCES
    src/common/folder_picker.c
    src/common/u4_transfer.c
    src/common/data_setup.c
    src/common/save_slots.c
    src/common/engine_settings.c
    src/audio/pcspeaker.c
	src/town.c
	src/mainout.c
	src/dungeon.c
	src/intro.c
	src/npc.c
	src/combat.c
	src/blckthrn.c
	src/lookobj.c
	src/dnglook.c
	src/outsubs.c
	src/shoppes.c
	src/endgame.c
	src/sjog.c
	src/cmds.c
	src/cast.c
	src/talk.c
	src/cast2.c
	src/zstats.c
	src/comsubs.c
	src/shoppes2.c
	src/shoppes3.c
	src/font.c
	src/vars.c
	src/ultima.c
	src/0000.c
	src/1000.c
	src/2000.c
	src/3000.c
	src/4000.c
	src/5000.c
	src/6000.c
	src/7000.c
	src/savegame.c
	src/grap_drv.c
	src/audio/audio.c
	src/audio/aud_mus.c
	src/audio/aud_sdl.c
	src/audio/aud_sfx.c
	src/backend/backend.c
	src/common/file.c
	src/common/util.c
    src/common/settings.c
	src/event/evt_sdl.c
	src/graphics/animate.c
	src/graphics/image.c
	src/graphics/grap_buf.c
	src/graphics/origin.c
	src/graphics/crt.c
	src/graphics/grap_sdl.c
	src/graphics/widescreen.c
	src/graphics/grap.c
	src/graphics/reveal.c
	src/graphics/wd.c
	src/common/debug.c
	src/common/lzw.c
	src/common/memory.c
	src/key/key_sdl.c
	src/key/mouse.c
	src/time/time_sdl.c
)

add_executable(ultima5 ${SOURCES})

option(U5D_BUILD_TESTS "Build native savegame regression tests" OFF)
set(U5D_TARGETS ultima5)
if(U5D_BUILD_TESTS)
    enable_testing()
    set(TEST_SOURCES ${SOURCES})
    add_executable(u5d_debug_test tests/debug_test.c src/common/debug.c)
    list(APPEND U5D_TARGETS u5d_debug_test)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/debug-runtime")
    add_test(NAME diagnostics COMMAND u5d_debug_test)
    set_tests_properties(diagnostics PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/debug-runtime" TIMEOUT 20)

    add_executable(u5d_pcspeaker_test tests/pcspeaker_test.c src/audio/pcspeaker.c)
    list(APPEND U5D_TARGETS u5d_pcspeaker_test)
    add_test(NAME pcspeaker_algorithms COMMAND u5d_pcspeaker_test)
    list(REMOVE_ITEM TEST_SOURCES src/ultima.c)
    add_executable(u5d_crt_test tests/crt_test.c ${TEST_SOURCES})
    list(APPEND U5D_TARGETS u5d_crt_test)
    if(UNIX AND NOT APPLE)
        target_link_options(u5d_crt_test PRIVATE "-Wl,--wrap=MOUSE_DrawFilteredCursor")
        target_compile_definitions(u5d_crt_test PRIVATE TEST_FILTERED_CURSOR)
    endif()
    add_test(NAME crt_filter COMMAND u5d_crt_test)
    set_tests_properties(crt_filter PROPERTIES TIMEOUT 10
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
    add_executable(u5d_savegame_test tests/savegame_test.c ${TEST_SOURCES})
    list(APPEND U5D_TARGETS u5d_savegame_test)
    add_executable(u5d_widescreen_test tests/widescreen_test.c ${TEST_SOURCES})
    list(APPEND U5D_TARGETS u5d_widescreen_test)
    add_test(NAME widescreen COMMAND u5d_widescreen_test)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/widescreen-runtime")
    set_tests_properties(widescreen PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/widescreen-runtime" TIMEOUT 10
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-runtime/SAVEGAME")
    foreach(NPC_FILE TOWNE.NPC DWELLING.NPC CASTLE.NPC KEEP.NPC)
        configure_file("${CMAKE_SOURCE_DIR}/Ultima 5/${NPC_FILE}"
                       "${CMAKE_BINARY_DIR}/test-runtime/${NPC_FILE}" COPYONLY)
    endforeach()
    add_test(NAME savegame COMMAND u5d_savegame_test)
    set_tests_properties(savegame PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/test-runtime")
    set_tests_properties(savegame PROPERTIES TIMEOUT 10)
    add_test(NAME missing_required_file COMMAND ${CMAKE_COMMAND}
             -DTEST_EXE=$<TARGET_FILE:u5d_savegame_test>
             -DTEST_DIR=${CMAKE_BINARY_DIR}/test-runtime
             -P ${CMAKE_SOURCE_DIR}/tests/check_missing_file.cmake)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(impera_u4_transfer_test tests/u4_transfer_test.c ${TEST_SOURCES})
        list(APPEND U5D_TARGETS impera_u4_transfer_test)
        target_compile_definitions(impera_u4_transfer_test PRIVATE TEST_GAME_DIR="${CMAKE_SOURCE_DIR}/Ultima 5")
        add_test(NAME u4_transfer COMMAND impera_u4_transfer_test)
        set_tests_properties(u4_transfer PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/test-runtime"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 10)
        add_executable(u5d_data_setup_test tests/data_setup_test.c ${TEST_SOURCES})
        list(APPEND U5D_TARGETS u5d_data_setup_test)
        target_link_options(u5d_data_setup_test PRIVATE "-Wl,--wrap=SDL_ShowOpenFolderDialog" "-Wl,--wrap=SDL_RenderPresent")
        target_compile_definitions(u5d_data_setup_test PRIVATE TEST_GAME_DIR="${CMAKE_SOURCE_DIR}/Ultima 5")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/setup-runtime")
        add_test(NAME data_setup COMMAND u5d_data_setup_test)
        set_tests_properties(data_setup PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/setup-runtime"
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software" TIMEOUT 10)
        add_executable(u5d_save_slots_test tests/save_slots_test.c ${TEST_SOURCES})
        list(APPEND U5D_TARGETS u5d_save_slots_test)
        target_link_options(u5d_save_slots_test PRIVATE "-Wl,--wrap=SDL_RenderPresent")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/slots-runtime")
        configure_file("${CMAKE_SOURCE_DIR}/Ultima 5/IBM.CH" "${CMAKE_BINARY_DIR}/slots-runtime/IBM.CH" COPYONLY)
        configure_file("${CMAKE_SOURCE_DIR}/Ultima 5/TILES.16" "${CMAKE_BINARY_DIR}/slots-runtime/TILES.16" COPYONLY)
        add_test(NAME save_slots COMMAND u5d_save_slots_test)
        set_tests_properties(save_slots PROPERTIES
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/slots-runtime"
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software" TIMEOUT 20)
        add_executable(u5d_engine_settings_test tests/engine_settings_test.c ${TEST_SOURCES})
        list(APPEND U5D_TARGETS u5d_engine_settings_test)
        add_test(NAME music_directory_picker COMMAND u5d_engine_settings_test music_picker)
        set_tests_properties(music_directory_picker PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/settings-runtime"
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software" TIMEOUT 10 RUN_SERIAL TRUE)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/settings-runtime")
        configure_file("${CMAKE_SOURCE_DIR}/Ultima 5/IBM.CH" "${CMAKE_BINARY_DIR}/settings-runtime/IBM.CH" COPYONLY)
        add_test(NAME engine_settings COMMAND u5d_engine_settings_test)
        foreach(exit_context settings menu cutscene gameplay)
            add_test(NAME ctrl_e_${exit_context} COMMAND u5d_engine_settings_test ${exit_context})
            set_tests_properties(ctrl_e_${exit_context} PROPERTIES
                WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/settings-runtime"
                ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
                TIMEOUT 10 RUN_SERIAL TRUE)
        endforeach()
        set_tests_properties(engine_settings PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/settings-runtime"
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software" TIMEOUT 10)
        add_test(NAME gameplay_engine_options COMMAND u5d_engine_settings_test gameplay_options)
        set_tests_properties(gameplay_engine_options PROPERTIES
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/settings-runtime"
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
            TIMEOUT 10 RUN_SERIAL TRUE)
        add_executable(u5d_audio_test tests/audio_test.c ${TEST_SOURCES})
        list(APPEND U5D_TARGETS u5d_audio_test)
        target_link_options(u5d_audio_test PRIVATE "-Wl,--wrap=SDL_PutAudioStreamData" "-Wl,--wrap=MIX_SetTrackAudio" "-Wl,--wrap=SDL_GetBasePath")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/audio-runtime")
        add_test(NAME audio COMMAND u5d_audio_test)
        set_tests_properties(audio PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/audio-runtime"
            ENVIRONMENT "SDL_AUDIO_DRIVER=dummy" TIMEOUT 10)
        add_executable(u5d_world_scroll_test tests/world_scroll_test.c ${TEST_SOURCES})
        list(APPEND U5D_TARGETS u5d_world_scroll_test)
        target_compile_definitions(u5d_world_scroll_test PRIVATE _FORTIFY_SOURCE=2)
        target_link_options(u5d_world_scroll_test PRIVATE
            "-Wl,--wrap=OUTSUBS_01b4" "-Wl,--wrap=ULTIMA_5e4a")
        add_test(NAME world_scroll COMMAND u5d_world_scroll_test)
        add_executable(u5d_mouse_test tests/mouse_test.c ${TEST_SOURCES})
        list(APPEND U5D_TARGETS u5d_mouse_test)
        target_link_options(u5d_mouse_test PRIVATE
            "-Wl,--wrap=ULTIMA_433e_AudioFootstep" "-Wl,--wrap=SDL_CreateColorCursor" "-Wl,--wrap=SDL_GetTicks" "-Wl,--wrap=SDL_GetMouseState"
            "-Wl,--wrap=SDL_RenderPresent" "-Wl,--wrap=SDL_Delay"
            "-Wl,--wrap=ULTIMA_1850_PrintString" "-Wl,--wrap=ULTIMA_16ba_PrintChar")
        add_test(NAME mouse_controls COMMAND u5d_mouse_test)
        add_test(NAME mouse_controls_opaque COMMAND u5d_mouse_test --opaque)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/mouse-runtime")
        set_tests_properties(mouse_controls mouse_controls_opaque PROPERTIES
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/mouse-runtime" TIMEOUT 10
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
        add_executable(u5d_combat_return_test tests/combat_return_test.c ${TEST_SOURCES})
        list(APPEND U5D_TARGETS u5d_combat_return_test)
        target_link_options(u5d_combat_return_test PRIVATE
            "-Wl,--wrap=DNGLOOK_117e" "-Wl,--wrap=COMBAT_0b94_MainLoop"
            "-Wl,--wrap=AUDIO_PlayBgmPerMap" "-Wl,--wrap=ULTIMA_2900_UpdateVitalsDisplay")
        add_test(NAME combat_return COMMAND u5d_combat_return_test)
        add_executable(u5d_file_case_test tests/file_case_test.c src/common/file.c src/common/debug.c)
        list(APPEND U5D_TARGETS u5d_file_case_test)
        target_include_directories(u5d_file_case_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/case-runtime/MiXeD/SubDir"
                            "${CMAKE_BINARY_DIR}/case-runtime/SaVeGaMe"
                            "${CMAKE_BINARY_DIR}/case-runtime/u4SaVe"
                            "${CMAKE_BINARY_DIR}/case-runtime/bGm")
        add_test(NAME case_insensitive_files COMMAND u5d_file_case_test)
        set_tests_properties(case_insensitive_files PROPERTIES
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/case-runtime" TIMEOUT 10)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        add_test(NAME runtime_filename_case COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tests/test_prepare_runtime.py)
    endif()
endif()

foreach(U5D_TARGET IN LISTS U5D_TARGETS)
target_include_directories(${U5D_TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${SDL3_INCLUDE_DIRS}
    ${SDL3_MIXER_INCLUDE_DIRS}
)

target_link_directories(${U5D_TARGET} PRIVATE
    ${SDL3_LIBRARY_DIRS}
    ${SDL3_MIXER_LIBRARY_DIRS}
)

target_link_libraries(${U5D_TARGET} PRIVATE
    ${SDL3_LIBRARIES}
    ${SDL3_MIXER_LIBRARIES}
)
if(IMPERA_BUNDLED_SDL)
    target_link_libraries(${U5D_TARGET} PRIVATE SDL3::SDL3-static SDL3_mixer::SDL3_mixer-static)
endif()
if(MSVC)
    target_compile_definitions(${U5D_TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(${U5D_TARGET} PRIVATE m)
endif()
endforeach()
