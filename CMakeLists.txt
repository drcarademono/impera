cmake_minimum_required(VERSION 3.16)
project(ultima5 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)
pkg_check_modules(SDL3_MIXER REQUIRED sdl3-mixer)

set(SOURCES
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
	src/graphics/grap_sdl.c
	src/graphics/grap.c
	src/graphics/reveal.c
	src/graphics/wd.c
	src/common/debug.c
	src/common/lzw.c
	src/common/memory.c
	src/key/key_sdl.c
	src/time/time_sdl.c
)

add_executable(ultima5 ${SOURCES})

option(U5D_BUILD_TESTS "Build native savegame regression tests" OFF)
set(U5D_TARGETS ultima5)
if(U5D_BUILD_TESTS)
    enable_testing()
    set(TEST_SOURCES ${SOURCES})
    list(REMOVE_ITEM TEST_SOURCES src/ultima.c)
    add_executable(u5d_savegame_test tests/savegame_test.c ${TEST_SOURCES})
    list(APPEND U5D_TARGETS u5d_savegame_test)
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
        add_executable(u5d_combat_return_test tests/combat_return_test.c ${TEST_SOURCES})
        list(APPEND U5D_TARGETS u5d_combat_return_test)
        target_link_options(u5d_combat_return_test PRIVATE
            "-Wl,--wrap=DNGLOOK_117e" "-Wl,--wrap=COMBAT_0b94_MainLoop"
            "-Wl,--wrap=AUDIO_PlayBgmPerMap" "-Wl,--wrap=ULTIMA_2900_UpdateVitalsDisplay")
        add_test(NAME combat_return COMMAND u5d_combat_return_test)
        add_executable(u5d_file_case_test tests/file_case_test.c src/common/file.c)
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
endforeach()
