# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

cmake_minimum_required(VERSION 3.20)
project(SubliminalHeadTracking VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP)
    # CMP0092 (NEW at our cmake_minimum_required) stops CMake injecting /W3, and
    # cl's own default is /W1. That is below the level at which C4244 and C4267
    # fire, and this codebase is largely size_t/uintptr_t/uint32_t arithmetic over
    # engine struct offsets, where a narrowing conversion is a wrong pointer
    # rather than a compile error. Applied per-target rather than globally so it
    # governs the code this repo owns, not the submodule or MinHook.
    # /WX because a warning nothing reads is not a gate: the point of raising
    # the level was to catch a narrowing conversion on an engine struct offset,
    # which is a wrong pointer rather than a compile error, and CI never opens
    # the build log. The tree is Level4-clean, so this costs nothing today.
    set(SUBLIMINAL_WARNINGS /W4 /WX)
endif()

# MinHook (for the GetPlayerViewPoint trampoline). Fetched + built locally;
# cameraunlock-core's hook manager links against this `minhook` target.
include(FetchContent)
FetchContent_Declare(
    minhook_src
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    # Pinned to the commit, not the tag: a tag is mutable and can be force-moved,
    # and this is the one build input fetched from the network rather than
    # vendored. Its code is compiled straight into the shipped .asi.
    GIT_TAG        9fbd087432700d73fc571118d6a9697a36443d88  # v1.3.3
)
FetchContent_MakeAvailable(minhook_src)
add_library(minhook STATIC
    ${minhook_src_SOURCE_DIR}/src/hook.c
    ${minhook_src_SOURCE_DIR}/src/buffer.c
    ${minhook_src_SOURCE_DIR}/src/trampoline.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde64.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC ${minhook_src_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /wd4267 /wd4244 /wd4127 /wd4131)
endif()

# Shared cameraunlock-core (C++ side) with the hook manager and UE runtime
# helpers enabled.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_UNREAL ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/view_hook.cpp
    src/mod_hotkeys.cpp
    src/camera_boundary.cpp
    src/camera_fov.cpp
    src/game_state.cpp
    src/aim_projection.cpp
    src/aim_trace.cpp
    src/lean_trace.cpp
    src/flashlight.cpp
    src/reticle_mover.cpp
    src/widget_probe.cpp
    src/ue_objects.cpp
    src/ue_reflect.cpp
    src/ue_vm.cpp
    src/config.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
)

add_library(SubliminalHeadTracking SHARED ${MOD_SOURCES})

# The log banner names the version, so an attached HeadTracking.log says which
# build produced it. release.ps1 stamps the project version above.
target_compile_definitions(SubliminalHeadTracking PRIVATE
    SUBLIMINAL_HT_VERSION="${PROJECT_VERSION}"
)

target_include_directories(SubliminalHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_compile_options(SubliminalHeadTracking PRIVATE ${SUBLIMINAL_WARNINGS})

target_link_libraries(SubliminalHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    cameraunlock_unreal
    minhook
    ws2_32
    psapi
)

# Output as .asi for Ultimate ASI Loader.
set_target_properties(SubliminalHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Behaviour locks for the parts of the mod that are pure functions - the
# tracker-to-Unreal camera boundary, the GetPlayerViewPoint caller gate, the
# reticle projection and the field-of-view axis model. A separate executable
# linking none of the hook or Windows machinery, so `pixi run test` runs
# anywhere the mod builds, with no game installed.
option(SUBLIMINAL_BUILD_TESTS "Build the behaviour tests" ON)
if(SUBLIMINAL_BUILD_TESTS)
    enable_testing()

    add_executable(subliminal_boundary_tests
        tests/boundary_tests.cpp
        src/camera_boundary.cpp
    )
    target_include_directories(subliminal_boundary_tests PRIVATE
        src
        cameraunlock-core/cpp/include
    )
    target_compile_options(subliminal_boundary_tests PRIVATE ${SUBLIMINAL_WARNINGS})
    add_test(NAME subliminal_boundary_tests COMMAND subliminal_boundary_tests)

    # Header-only on the mod side: the projection and the tangent model are both
    # inline, which is what lets the reticle's arithmetic be locked without the
    # engine.
    add_executable(subliminal_projection_tests tests/projection_tests.cpp)
    target_include_directories(subliminal_projection_tests PRIVATE
        src
        cameraunlock-core/cpp/include
    )
    target_compile_options(subliminal_projection_tests PRIVATE ${SUBLIMINAL_WARNINGS})
    add_test(NAME subliminal_projection_tests COMMAND subliminal_projection_tests)

    # config.cpp reads through core's IniReader and logs through core's file log,
    # so this one links cameraunlock. Still no hooks and no game.
    add_executable(subliminal_config_tests
        tests/config_tests.cpp
        src/config.cpp
    )
    target_include_directories(subliminal_config_tests PRIVATE
        src
        cameraunlock-core/cpp/include
    )
    target_link_libraries(subliminal_config_tests PRIVATE cameraunlock)
    target_compile_options(subliminal_config_tests PRIVATE ${SUBLIMINAL_WARNINGS})
    add_test(NAME subliminal_config_tests COMMAND subliminal_config_tests)

    # Timing lock rather than a behaviour lock: it drives the real receiver over
    # a real loopback socket and measures how long the mod takes to reclaim the
    # tracker port after whatever was holding it exits. Sockets and threads, no
    # hooks and no game.
    add_executable(subliminal_udp_recovery_tests tests/udp_recovery_tests.cpp)
    target_include_directories(subliminal_udp_recovery_tests PRIVATE
        src
        cameraunlock-core/cpp/include
    )
    target_link_libraries(subliminal_udp_recovery_tests PRIVATE cameraunlock ws2_32)
    target_compile_options(subliminal_udp_recovery_tests PRIVATE ${SUBLIMINAL_WARNINGS})
    add_test(NAME subliminal_udp_recovery_tests COMMAND subliminal_udp_recovery_tests)
endif()
