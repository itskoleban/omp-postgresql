cmake_minimum_required(VERSION 3.24)
project(
    omp-postgresql
    VERSION 0.1.0
    LANGUAGES C CXX
)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
option(PG_BUILD_COMPONENT "Build the open.mp component" ON)
option(PG_SANITIZE "Enable address and undefined behavior sanitizers" OFF)
option(PG_BUILD_BENCHMARK "Build standalone libpq and Engine benchmarks" OFF)
set(PG_PAWN_INCLUDE_DIR
    "qawno/include"
    CACHE STRING "Pawn include installation directory"
)
find_package(PostgreSQL 17 REQUIRED)
find_package(Threads REQUIRED)
add_library(
    pg_core STATIC src/config.cpp src/parameters.cpp src/statement.cpp src/session.cpp
                   src/engine.cpp
)
set_target_properties(pg_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pg_core PUBLIC include)
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
            "{\n\"version\": \"${PROJECT_VERSION}\",\n\"commit\": \"${PG_SOURCE_COMMIT}\",\n\"configuration\": \"$<CONFIG>\",\n\"system\": \"${CMAKE_SYSTEM_NAME}\",\n\"pointer_bytes\": ${CMAKE_SIZEOF_VOID_P},\n\"compiler\": \"${CMAKE_CXX_COMPILER_ID}\",\n\"compiler_version\": \"${CMAKE_CXX_COMPILER_VERSION}\",\n\"cxx_standard\": 20,\n\"libpq_version\": \"${PostgreSQL_VERSION_STRING}\",\n\"sanitize\": \"${PG_SANITIZE}\",\n\"sdk_commit\": \"473b732b2de159773b2e2908fd11aa99f07db284\",\n\"pawn_commit\": \"29f1a8c7fd2e01929807cd8c50353fbc93bbd651\"\n}\n"
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
    add_executable(pg_tests tests/tests.cpp)
    add_executable(pg_extension_tests tests/engine_extensions.cpp)
    add_executable(pg_type_tests tests/type_tests.cpp)
    add_executable(pg_stress_tests tests/stress.cpp)
    if(WIN32)
        target_link_libraries(pg_stress_tests PRIVATE psapi)
    endif()
    foreach(test_target pg_tests pg_extension_tests pg_type_tests pg_stress_tests)
        target_link_libraries(${test_target} PRIVATE pg_core)
        if(PG_SANITIZE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # GCC's ASan runtime can fail before main with PIE and high-entropy ASLR. Keep this
            # workaround on test executables; the component remains PIC.
            target_compile_options(${test_target} PRIVATE -fno-pie)
            target_link_options(${test_target} PRIVATE -no-pie)
        endif()
    endforeach()
    add_test(NAME unit COMMAND pg_tests)
    add_test(NAME integration COMMAND pg_tests --integration)
    add_test(NAME types_unit COMMAND pg_type_tests)
    add_test(NAME types_integration COMMAND pg_type_tests --integration)
    add_test(NAME extensions_integration COMMAND pg_extension_tests)
    add_test(NAME stress_integration COMMAND pg_stress_tests)
    set_tests_properties(
        stress_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 LABELS stress
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
                PG_BENCH_SANITIZE="${PG_SANITIZE}"
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
