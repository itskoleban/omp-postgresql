cmake_minimum_required(VERSION 3.24)
project(
    omp-postgresql
    VERSION 0.1.0
    LANGUAGES C CXX
)
# Zero selects a stable source identity; positive values retain the RC route.
# This is intentionally source-controlled, never a cache/build override.
set(PG_RELEASE_CANDIDATE 0)
if(PG_RELEASE_CANDIDATE EQUAL 0)
    set(PG_DISTRIBUTION_VERSION "${PROJECT_VERSION}")
    set(PG_RELEASE_CHANNEL "stable")
else()
    set(PG_DISTRIBUTION_VERSION "${PROJECT_VERSION}-rc.${PG_RELEASE_CANDIDATE}")
    set(PG_RELEASE_CHANNEL "rc")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
option(PG_BUILD_COMPONENT "Build the open.mp component" ON)
option(PG_SANITIZE "Enable address and undefined behavior sanitizers" OFF)
option(PG_THREAD_SANITIZE "Enable ThreadSanitizer (Linux x64 qualification only)" OFF)
set(PG_STRESS_SECONDS
    "0"
    CACHE STRING "Minimum sustained stress load duration (0..86400 seconds)"
)
if(NOT PG_STRESS_SECONDS MATCHES "^(0|[1-9][0-9]*)$" OR PG_STRESS_SECONDS GREATER 86400)
    message(FATAL_ERROR "PG_STRESS_SECONDS must be a decimal integer in 0..86400")
endif()
if(PG_THREAD_SANITIZE)
    if(PG_SANITIZE)
        message(FATAL_ERROR "ThreadSanitizer and ASan/UBSan require separate build directories")
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
       OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
       OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$"
    )
        message(FATAL_ERROR "PG_THREAD_SANITIZE requires a GNU/Clang Linux 64-bit toolchain")
    endif()
endif()
set(PG_SANITIZER_LABEL "${PG_SANITIZE}")
if(PG_THREAD_SANITIZE)
    set(PG_SANITIZER_LABEL "THREAD")
endif()
option(PG_BUILD_BENCHMARK "Build standalone libpq and Engine benchmarks" OFF)
set(PG_LIBPQ_SOURCE_DIR
    ""
    CACHE PATH "Windows libpq source for its mutex regression test"
)
set(PG_LOG_ENOSPC_BASE
    ""
    CACHE PATH "Isolated tiny Linux tmpfs for actual logger ENOSPC qualification"
)
set(PG_PAWN_INCLUDE_DIR
    "qawno/include"
    CACHE STRING "Pawn include installation directory"
)
find_package(PostgreSQL 17 REQUIRED)
find_package(Threads REQUIRED)
add_library(
    pg_core STATIC
    src/config.cpp
    src/diagnostics.cpp
    src/log_file.cpp
    src/parameters.cpp
    src/statement.cpp
    src/sql_sections.cpp
    src/session.cpp
    src/engine.cpp
)
set_target_properties(pg_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pg_core PUBLIC include)
target_compile_definitions(
    pg_core
    PUBLIC PG_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} PG_VERSION_MINOR=${PROJECT_VERSION_MINOR}
           PG_VERSION_PATCH=${PROJECT_VERSION_PATCH} PG_VERSION_PRERELEASE=${PG_RELEASE_CANDIDATE}
           PG_VERSION_STRING="${PG_DISTRIBUTION_VERSION}"
)
target_link_libraries(pg_core PUBLIC PostgreSQL::PostgreSQL Threads::Threads)
if(WIN32)
    target_compile_definitions(pg_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(pg_core PUBLIC ws2_32)
endif()
if(MSVC)
    target_compile_definitions(pg_core PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(pg_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(pg_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(PG_SANITIZE AND NOT MSVC)
    target_compile_options(pg_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(pg_core PUBLIC -fsanitize=address,undefined)
endif()
if(PG_THREAD_SANITIZE)
    target_compile_options(pg_core PUBLIC -fsanitize=thread -fno-omit-frame-pointer -g -O1)
    target_link_options(pg_core PUBLIC -fsanitize=thread)
endif()
if(PG_BUILD_COMPONENT)
    include(FetchContent)
    FetchContent_Declare(
        omp_sdk
        GIT_REPOSITORY https://github.com/openmultiplayer/open.mp-sdk.git
        GIT_TAG 473b732b2de159773b2e2908fd11aa99f07db284
    )
    FetchContent_Declare(
        pawn
        GIT_REPOSITORY https://github.com/openmultiplayer/compiler.git
        GIT_TAG 29f1a8c7fd2e01929807cd8c50353fbc93bbd651
        SOURCE_SUBDIR headers-only
    )
    FetchContent_MakeAvailable(omp_sdk pawn)
    # SDK header dependencies are build inputs, not files to install into the game server.
    set_property(DIRECTORY ${omp_sdk_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE)
    add_library(omp-postgresql SHARED src/component.cpp src/natives.cpp)
    target_include_directories(
        omp-postgresql PRIVATE ${pawn_SOURCE_DIR}/source ${pawn_SOURCE_DIR}/source/linux
    )
    target_compile_definitions(omp-postgresql PRIVATE HAVE_STDINT_H=1 PAWN_CELL_SIZE=32)
    if(WIN32)
        target_compile_definitions(
            omp-postgresql PRIVATE HAVE_ALLOCA_H=0 WIN32_LEAN_AND_MEAN NOMINMAX
        )
    endif()
    target_link_libraries(omp-postgresql PRIVATE pg_core OMP-SDK)
    set_target_properties(omp-postgresql PROPERTIES PREFIX "")
    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE PG_SOURCE_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE PG_GIT_RESULT
        ERROR_QUIET
    )
    if(NOT PG_GIT_RESULT EQUAL 0)
        # Source archive builds remain supported; distribution assembly requires Git provenance.
        set(PG_SOURCE_COMMIT "unknown")
    endif()
    file(
        GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/package-build-info-$<CONFIG>.json"
        CONTENT
            "{\n\"version\": \"${PG_DISTRIBUTION_VERSION}\",\n\"release_channel\": \"${PG_RELEASE_CHANNEL}\",\n\"commit\": \"${PG_SOURCE_COMMIT}\",\n\"configuration\": \"$<CONFIG>\",\n\"system\": \"${CMAKE_SYSTEM_NAME}\",\n\"pointer_bytes\": ${CMAKE_SIZEOF_VOID_P},\n\"compiler\": \"${CMAKE_CXX_COMPILER_ID}\",\n\"compiler_version\": \"${CMAKE_CXX_COMPILER_VERSION}\",\n\"cxx_standard\": 20,\n\"libpq_version\": \"${PostgreSQL_VERSION_STRING}\",\n\"sanitize\": \"${PG_SANITIZER_LABEL}\",\n\"sdk_commit\": \"473b732b2de159773b2e2908fd11aa99f07db284\",\n\"pawn_commit\": \"29f1a8c7fd2e01929807cd8c50353fbc93bbd651\"\n}\n"
    )
    install(
        TARGETS omp-postgresql
        LIBRARY DESTINATION components COMPONENT omp-postgresql
        RUNTIME DESTINATION components COMPONENT omp-postgresql
    )
    install(
        FILES ${omp_sdk_SOURCE_DIR}/LICENSE.md
        DESTINATION share/omp-postgresql/licenses/open.mp-sdk
        COMPONENT omp-postgresql
    )
    install(
        FILES ${pawn_SOURCE_DIR}/license.txt
        DESTINATION share/omp-postgresql/licenses/pawn
        COMPONENT omp-postgresql
    )
    install(
        FILES ${omp_sdk_SOURCE_DIR}/lib/glm/copying.txt
        DESTINATION share/omp-postgresql/licenses/glm
        COMPONENT omp-postgresql
    )
    install(
        FILES ${omp_sdk_SOURCE_DIR}/lib/robin-hood-hashing/LICENSE
        DESTINATION share/omp-postgresql/licenses/robin-hood-hashing
        COMPONENT omp-postgresql
    )
    install(
        FILES ${omp_sdk_SOURCE_DIR}/lib/span-lite/LICENSE.txt
        DESTINATION share/omp-postgresql/licenses/span-lite
        COMPONENT omp-postgresql
    )
    install(
        FILES ${omp_sdk_SOURCE_DIR}/lib/string-view-lite/LICENSE.txt
        DESTINATION share/omp-postgresql/licenses/string-view-lite
        COMPONENT omp-postgresql
    )
endif()
include(CTest)
if(BUILD_TESTING)
    if(WIN32 AND PG_LIBPQ_SOURCE_DIR)
        # Compile the dependency's actual C implementation; hooks only force scheduling.
        add_executable(
            pg_libpq_mutex_tests tests/libpq_mutex/test.c
                                 ${PG_LIBPQ_SOURCE_DIR}/src/interfaces/libpq/pthread-win32.c
        )
        target_include_directories(
            pg_libpq_mutex_tests PRIVATE tests/libpq_mutex ${PG_LIBPQ_SOURCE_DIR}/src/port
        )
        add_test(NAME libpq_windows_mutex_unit COMMAND pg_libpq_mutex_tests)
        set_tests_properties(libpq_windows_mutex_unit PROPERTIES TIMEOUT 15)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(
            NAME libpq_process_startup_integration
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/repeat_connection_startup.py --binary
                $<TARGET_FILE:pg_connection_tests>
        )
        set_tests_properties(
            libpq_process_startup_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120
        )
    endif()
    if(PG_BUILD_COMPONENT)
        # Exercise the actual native wrappers with the pinned SDK, including sanitizer builds.
        add_executable(pg_native_tests tests/native_tests.cpp src/component.cpp src/natives.cpp)
        target_include_directories(
            pg_native_tests PRIVATE ${pawn_SOURCE_DIR}/source ${pawn_SOURCE_DIR}/source/linux
        )
        target_compile_definitions(
            pg_native_tests
            PRIVATE HAVE_STDINT_H=1 PAWN_CELL_SIZE=32
                    PG_PAWN_API_PATH="${CMAKE_CURRENT_SOURCE_DIR}/pawn/omp-postgresql.inc"
        )
        if(WIN32)
            target_compile_definitions(pg_native_tests PRIVATE HAVE_ALLOCA_H=0)
        endif()
        target_link_libraries(pg_native_tests PRIVATE pg_core OMP-SDK)
        if(PG_SANITIZE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_options(pg_native_tests PRIVATE -fno-pie)
            target_link_options(pg_native_tests PRIVATE -no-pie)
        endif()
        add_test(NAME natives_unit COMMAND pg_native_tests)
        add_test(NAME natives_integration COMMAND pg_native_tests --integration)
        add_test(NAME logging_integration COMMAND pg_native_tests --logging-benchmark)
        set_tests_properties(natives_unit PROPERTIES TIMEOUT 30)
        set_tests_properties(natives_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
        set_tests_properties(
            logging_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90 LABELS benchmark
        )
    endif()
    add_executable(pg_tests tests/tests.cpp)
    add_executable(pg_diagnostics_tests tests/diagnostics.cpp)
    add_executable(pg_extension_tests tests/engine_extensions.cpp)
    add_executable(pg_type_tests tests/type_tests.cpp)
    add_executable(pg_stress_tests tests/stress.cpp)
    add_executable(pg_connection_tests tests/connection_lifecycle.cpp)
    add_executable(pg_connection_stall_tests tests/connection_stalls.cpp)
    add_executable(pg_sql_section_tests tests/sql_sections.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT PG_SANITIZE)
        add_executable(pg_resource_exhaustion tests/resource_exhaustion.cpp)
        target_link_libraries(pg_resource_exhaustion PRIVATE pg_core)
        if(PG_SANITIZE)
            target_compile_options(pg_resource_exhaustion PRIVATE -fno-pie)
            target_link_options(pg_resource_exhaustion PRIVATE -no-pie)
        endif()
        add_test(NAME resource_exhaustion_integration COMMAND pg_resource_exhaustion)
        set_tests_properties(resource_exhaustion_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PG_SANITIZE)
        message(STATUS "NOT RUN: total FD exhaustion with ASan/UBSan; runtime vptr checks require pipe descriptors (see audit/final-allocation.md)")
    endif()
    add_executable(pg_worker_allocation tests/worker_allocation.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(pg_worker_allocation PRIVATE -fno-pie)
        target_link_options(pg_worker_allocation PRIVATE -no-pie -rdynamic)
    endif()
    if(PG_THREAD_SANITIZE)
        # Clang TSan supplies strong global new/delete interceptors. Keep them intact.
        # Named checkpoints in the actual Lane body model allocation exceptions
        # without replacing TSan's global allocator interceptors.
        target_compile_definitions(pg_worker_allocation PRIVATE PG_TEST_ALLOCATIONS=2)
        add_test(NAME worker_site_allocation_integration COMMAND pg_worker_allocation)
        set_tests_properties(worker_site_allocation_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME worker_site_counter_isolation COMMAND pg_worker_allocation --check-counter-isolation)
        foreach(scenario recovery shutdown)
            add_test(NAME worker_site_requires_${scenario} COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_worker_faults.py
                     $<TARGET_FILE:pg_worker_allocation> ${scenario})
            set_tests_properties(worker_site_requires_${scenario} PROPERTIES TIMEOUT 30)
        endforeach()
        set_tests_properties(worker_site_counter_isolation PROPERTIES TIMEOUT 30)
        add_executable(pg_tsan_race_control tests/tsan_race_control.cpp)
        target_compile_options(pg_tsan_race_control PRIVATE -fsanitize=thread -g -O1)
        target_link_options(pg_tsan_race_control PRIVATE -fsanitize=thread)
        add_executable(pg_tsan_startup_control tests/tsan_startup_control.cpp)
        target_compile_options(pg_tsan_startup_control PRIVATE -fsanitize=thread -g -O1)
        target_link_options(pg_tsan_startup_control PRIVATE -fsanitize=thread)
        add_test(NAME tsan_detector_control COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_tsan_control.py $<TARGET_FILE:pg_tsan_race_control>
                 --startup-probe $<TARGET_FILE:pg_tsan_startup_control> --runs 20)
        set_tests_properties(tsan_detector_control PROPERTIES TIMEOUT 30)
    else()
        target_compile_definitions(pg_worker_allocation PRIVATE PG_TEST_ALLOCATIONS=1)
        add_test(NAME worker_allocation_integration COMMAND pg_worker_allocation)
        set_tests_properties(worker_allocation_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
    endif()
    add_executable(pg_terminal_allocation tests/terminal_allocation.cpp)
    if(PG_THREAD_SANITIZE)
        target_compile_definitions(pg_terminal_allocation PRIVATE PG_TEST_ALLOCATIONS=0)
        add_test(NAME terminal_queue_unit COMMAND pg_terminal_allocation)
        set_tests_properties(terminal_queue_unit PROPERTIES TIMEOUT 30)
    else()
        target_compile_definitions(pg_terminal_allocation PRIVATE PG_TEST_ALLOCATIONS=1)
        add_test(NAME terminal_allocation_unit COMMAND pg_terminal_allocation)
        set_tests_properties(terminal_allocation_unit PROPERTIES TIMEOUT 30)
    endif()
    add_executable(pg_audit_fuzz tests/audit_fuzz.cpp)
    add_executable(pg_audit_faults tests/audit_faults.cpp)
    add_executable(pg_network_recovery tests/network_recovery.cpp)
    # Source seams compile the actual Lane body; the archive supplies Engine/helpers.
    add_executable(pg_socket_wait_tests tests/socket_wait.cpp)
    if(WIN32)
        target_link_libraries(pg_stress_tests PRIVATE psapi)
    endif()
    foreach(
        test_target
        pg_tests
        pg_extension_tests
        pg_type_tests
        pg_stress_tests
        pg_connection_tests
        pg_connection_stall_tests
        pg_sql_section_tests
        pg_diagnostics_tests
        pg_audit_fuzz
        pg_audit_faults
        pg_network_recovery
        pg_socket_wait_tests
        pg_terminal_allocation
        pg_worker_allocation
    )
        target_link_libraries(${test_target} PRIVATE pg_core)
        if(PG_SANITIZE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # GCC's ASan runtime can fail before main with PIE and high-entropy ASLR. Keep this
            # workaround on test executables; the component remains PIC.
            target_compile_options(${test_target} PRIVATE -fno-pie)
            target_link_options(${test_target} PRIVATE -no-pie)
        endif()
    endforeach()
    add_test(NAME unit COMMAND pg_tests)
    add_test(NAME audit_semantic_fuzz COMMAND pg_audit_fuzz)
    add_test(NAME socket_wait_unit COMMAND pg_socket_wait_tests)
    set_tests_properties(socket_wait_unit PROPERTIES TIMEOUT 30)
    set_tests_properties(audit_semantic_fuzz PROPERTIES TIMEOUT 60 LABELS "audit;fuzz")
    add_test(NAME audit_faults_integration COMMAND pg_audit_faults)
    set_tests_properties(audit_faults_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "audit;stress")
    add_test(NAME network_recovery_integration COMMAND pg_network_recovery)
    set_tests_properties(network_recovery_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90 LABELS "network;recovery")
    add_test(NAME diagnostics_unit COMMAND pg_diagnostics_tests)
    set_tests_properties(diagnostics_unit PROPERTIES TIMEOUT 30)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME diagnostics_short_write COMMAND pg_diagnostics_tests --short-write)
        add_test(NAME diagnostics_permission COMMAND pg_diagnostics_tests --permission)
        set_tests_properties(diagnostics_short_write diagnostics_permission PROPERTIES TIMEOUT 15)
        set_tests_properties(diagnostics_permission PROPERTIES SKIP_RETURN_CODE 77)
        if(PG_LOG_ENOSPC_BASE)
            add_test(NAME diagnostics_enospc COMMAND pg_diagnostics_tests --enospc
                                                     "${PG_LOG_ENOSPC_BASE}"
            )
            set_tests_properties(diagnostics_enospc PROPERTIES TIMEOUT 45)
        endif()
    endif()
    add_test(NAME integration COMMAND pg_tests --integration)
    add_test(NAME types_unit COMMAND pg_type_tests)
    add_test(NAME types_integration COMMAND pg_type_tests --integration)
    add_test(NAME extensions_integration COMMAND pg_extension_tests)
    add_test(NAME stress_integration COMMAND pg_stress_tests)
    add_test(NAME connection_lifecycle_integration COMMAND pg_connection_tests)
    add_test(NAME connection_stalls_integration COMMAND pg_connection_stall_tests)
    add_test(NAME sql_sections_unit COMMAND pg_sql_section_tests)
    add_test(NAME sql_sections_integration COMMAND pg_sql_section_tests --integration)
    set_tests_properties(sql_sections_unit PROPERTIES TIMEOUT 30)
    set_tests_properties(sql_sections_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
    set_tests_properties(
        connection_stalls_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90 LABELS stress
    )
    set_tests_properties(
        connection_lifecycle_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90 LABELS stress
    )
    math(EXPR PG_STRESS_TIMEOUT "180 + ${PG_STRESS_SECONDS}")
    set_tests_properties(
        stress_integration
        PROPERTIES SKIP_RETURN_CODE
                   77
                   TIMEOUT
                   ${PG_STRESS_TIMEOUT}
                   LABELS
                   stress
                   ENVIRONMENT
                   "OMP_PG_STRESS_SECONDS=${PG_STRESS_SECONDS}"
    )
    set_tests_properties(unit types_unit PROPERTIES TIMEOUT 30)
    set_tests_properties(
        integration types_integration extensions_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT
                                                                        90
    )
endif()
install(
    FILES pawn/omp-postgresql.inc
    DESTINATION ${PG_PAWN_INCLUDE_DIR}
    COMPONENT omp-postgresql
)
install(
    FILES LICENSE NOTICE.md
    DESTINATION share/omp-postgresql
    COMPONENT omp-postgresql
)
install(
    FILES scripts/patches/README.md scripts/patches/libpq-windows-mutex.patch
    DESTINATION share/omp-postgresql/patches
    COMPONENT omp-postgresql
)
install(
    FILES README.md LICENSE NOTICE.md DEPENDENCIES.md
    DESTINATION .
    COMPONENT omp-postgresql
)
install(
    DIRECTORY docs/
    DESTINATION docs
    COMPONENT omp-postgresql
    FILES_MATCHING
    PATTERN "*.md"
)
install(
    DIRECTORY examples/
    DESTINATION examples
    COMPONENT omp-postgresql
    FILES_MATCHING
    PATTERN "*.pwn"
    PATTERN "pg_service.conf"
)
install(
    FILES examples/sql/accounts.sql examples/sql/tutorial-select.sql examples/sql/tutorial-auth.sql
    DESTINATION examples/sql
    COMPONENT omp-postgresql
)
install(
    FILES examples/postgresql.sql examples/pg_hba.conf
    DESTINATION examples
    COMPONENT omp-postgresql
)
if(PG_BUILD_BENCHMARK)
    add_executable(pg_benchmark tests/benchmark.cpp)
    target_link_libraries(pg_benchmark PRIVATE pg_core)
    add_executable(pg_engine_benchmark tests/engine_benchmark.cpp)
    target_link_libraries(pg_engine_benchmark PRIVATE pg_core)
    if(WIN32)
        target_link_libraries(pg_engine_benchmark PRIVATE psapi)
    endif()
    find_package(Git QUIET)
    set(PG_BENCH_COMMIT unknown)
    set(PG_BENCH_TREE unavailable)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE PG_BENCH_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        RESULT_VARIABLE PG_BENCH_GIT_RESULT
    )
    if(PG_BENCH_GIT_RESULT EQUAL 0)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=normal
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE PG_BENCH_STATUS COMMAND_ERROR_IS_FATAL ANY
        )
        set(PG_BENCH_TREE clean)
        if(PG_BENCH_STATUS)
            set(PG_BENCH_TREE dirty)
        endif()
    else()
        # Source archives can benchmark, but cannot claim a verified Git identity.
        set(PG_BENCH_COMMIT unknown)
    endif()
    target_compile_definitions(
        pg_engine_benchmark
        PRIVATE PG_BENCH_COMMIT="${PG_BENCH_COMMIT}"
                PG_BENCH_TREE="${PG_BENCH_TREE}"
                PG_BENCH_CONFIG="$<CONFIG>"
                PG_BENCH_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
                PG_BENCH_SANITIZE="${PG_SANITIZER_LABEL}"
    )
    if(MSVC)
        target_compile_definitions(pg_engine_benchmark PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(pg_engine_benchmark PRIVATE /W4 /permissive-)
    else()
        target_compile_options(pg_engine_benchmark PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(PG_SANITIZE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(pg_engine_benchmark PRIVATE -fno-pie)
        target_link_options(pg_engine_benchmark PRIVATE -no-pie)
    endif()
    if(BUILD_TESTING)
        add_test(NAME engine_benchmark_smoke COMMAND pg_engine_benchmark 41 2)
        set_tests_properties(
            engine_benchmark_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90 LABELS benchmark
        )
    endif()
endif()
