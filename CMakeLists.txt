cmake_minimum_required(VERSION 3.20)
project(WorldSimulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ws_core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/profile_resolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/interactions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/observability.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/replay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/run_signature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/state_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/subsystems/bootstrap_subsystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/subsystems/subsystems.cpp
)

target_include_directories(ws_core PUBLIC include)

add_executable(world_sim ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
target_link_libraries(world_sim PRIVATE ws_core)

enable_testing()

function(ws_add_test test_name source_relative_path)
    set(test_source "${CMAKE_CURRENT_SOURCE_DIR}/${source_relative_path}")
    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} PRIVATE ws_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

ws_add_test(determinism_core_test tests/determinism_core.cpp)
ws_add_test(state_store_engine_test tests/state_store_engine.cpp)
ws_add_test(simulation_loop_test tests/simulation_loop.cpp)
ws_add_test(subsystems_test tests/subsystems.cpp)
ws_add_test(interactions_test tests/interactions.cpp)
ws_add_test(advanced_coupling_test tests/advanced_coupling.cpp)
ws_add_test(control_replay_test tests/control_replay.cpp)
ws_add_test(parallel_stability_test tests/parallel_stability.cpp)
