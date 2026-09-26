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
    src/legacy_config/legacy_config.cpp
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

    # The canonical config, the committed file and the toggles' saves. Also the
    # renderer behind `pixi run render-config` (--render-config <path>). Links
    # cameraunlock for the config owner and the file log; still no hooks and no
    # game.
    add_executable(subliminal_config_tests
        tests/config_tests.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(subliminal_config_tests PRIVATE
        src
        cameraunlock-core/cpp/include
    )
    target_link_libraries(subliminal_config_tests PRIVATE cameraunlock)
    target_compile_definitions(subliminal_config_tests PRIVATE SUBLIMINAL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
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

    # The canonical config conversion's differential test: v0.1.0's reader
    # (byte copies in tests/config_differential/oracle/src) and startup code
    # against the frozen reader in src/legacy_config/, and that reader against
    # the config owner's migration into CameraUnlock.ini.
    set(SUBLIMINAL_DIFFERENTIAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    add_executable(subliminal_config_differential
        ${SUBLIMINAL_DIFFERENTIAL_DIR}/differential_tests.cpp
        ${SUBLIMINAL_DIFFERENTIAL_DIR}/oracle/oracle_reader.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(subliminal_config_differential PRIVATE
        src
        ${SUBLIMINAL_DIFFERENTIAL_DIR}
        cameraunlock-core/cpp/include
    )
    target_compile_definitions(subliminal_config_differential PRIVATE
        SUBLIMINAL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(subliminal_config_differential PRIVATE cameraunlock)
    target_compile_options(subliminal_config_differential PRIVATE ${SUBLIMINAL_WARNINGS})
    add_test(NAME subliminal_config_differential COMMAND subliminal_config_differential)

    # Core's canonical config lint over the committed file and every file the
    # differential test migrated, which it writes beside itself under migrated\.
    add_test(NAME subliminal_config_lint COMMAND node
        ${SUBLIMINAL_DIFFERENTIAL_DIR}/lint-migrated.mjs
        $<TARGET_FILE_DIR:subliminal_config_differential>/migrated)
    set_tests_properties(subliminal_config_lint PROPERTIES DEPENDS subliminal_config_differential)

    # What the differential test compiles is pinned by hash, line endings aside:
    # the oracle's byte copies must stay v0.1.0's (git show v0.1.0:src/<file>),
    # the core sources both readers compile must hold the code v0.1.0 was built
    # with (core 665a345), and the frozen reader is never edited.
    function(subliminal_require_hash path expected)
        file(READ "${path}" text)
        string(REPLACE "\r" "" text "${text}")
        string(SHA256 actual "${text}")
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${path} has changed (SHA-256 of its text ${actual}, pinned "
                                "${expected}). The config differential test compares against "
                                "exactly this file; see CMakeLists.txt.")
        endif()
    endfunction()
    set(SUBLIMINAL_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp)
    foreach(pinned
            "${SUBLIMINAL_DIFFERENTIAL_DIR}/oracle/src/config.cpp|bec94b8b52124937e168307dcf2e0f89a929a113787ffa40eb7dded69d414572"
            "${SUBLIMINAL_DIFFERENTIAL_DIR}/oracle/src/config.h|ff8f034029b28b14bc5670a7209a486fc595b0abd11bdfc16c4eb4d419ded84a"
            "${SUBLIMINAL_DIFFERENTIAL_DIR}/oracle/src/logging.h|f034169712d24f324da281174b9ce169f3064a96457d10d0ca248291dbec1223"
            "${SUBLIMINAL_CORE_DIR}/include/cameraunlock/config/ini_reader.h|a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"
            "${SUBLIMINAL_CORE_DIR}/src/config/ini_reader.cpp|e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"
            "${SUBLIMINAL_CORE_DIR}/include/cameraunlock/config/value_guards.h|6d3e3512bdfc9ac2a54bee750c5425191bdb4c24f3ae2ea3b9925612e01c78e5"
            "${SUBLIMINAL_CORE_DIR}/src/config/value_guards.cpp|a833ff7f2721f7974ce039a3597349a0974e6695ce3e75e598ce5bf4985a14b1"
            "${SUBLIMINAL_CORE_DIR}/include/cameraunlock/math/finite_utils.h|c59772d698d54ade3374ee1221b74f5563a86d76f0eb34a989ef7efab389c0ad"
            "${SUBLIMINAL_CORE_DIR}/include/cameraunlock/protocol/port_utils.h|91bff564d5e279b66527ec5553afcf4d591812dab0e78f71a48ef412e4db7a44"
            "${SUBLIMINAL_CORE_DIR}/include/cameraunlock/logging/file_log.h|43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"
            "${SUBLIMINAL_CORE_DIR}/src/logging/file_log.cpp|73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"
            "${SUBLIMINAL_CORE_DIR}/include/cameraunlock/camera/lean_clamp.h|a53cf2d4c0060643d2cec5308f0f979bf5868c4f892a62c9bbe4572c427c2a8b"
            "${SUBLIMINAL_CORE_DIR}/include/cameraunlock/data/position_settings.h|b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"
            "${SUBLIMINAL_CORE_DIR}/include/cameraunlock/effects/head_follow_light.h|975470cfa10cb5c00689a2fc5aa31c82fef151a2a607212b21dd371895b0589f"
            "${SUBLIMINAL_CORE_DIR}/include/cameraunlock/math/smoothing_utils.h|fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"
            "${SUBLIMINAL_CORE_DIR}/include/cameraunlock/math/vec3.h|021c45902da44d12911a7cacd9b76fa28775b382583dca2e5e9df3888e242683"
            "${SUBLIMINAL_CORE_DIR}/include/cameraunlock/math/angle_utils.h|d7a905270933e3cb0c4c361d29d3fd701655498cbcd1875ea79d180468bdbe6a"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.h|def85ceeefcb505c7597260dd4cf4a5c2805239f60cd5aec5bba813d08242187"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.cpp|3fdebe7106e160c6100885851a401a6594247d4be64a1bf18c816ccd45a38292")
        string(REPLACE "|" ";" pair "${pinned}")
        list(GET pair 0 pinned_path)
        list(GET pair 1 pinned_hash)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pinned_path}")
        subliminal_require_hash("${pinned_path}" "${pinned_hash}")
    endforeach()
endif()
