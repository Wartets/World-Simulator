cmake_minimum_required(VERSION 3.20)
project(WorldSimulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ws_core
    src/core/profile_resolver.cpp
    src/core/interactions.cpp
    src/core/run_signature.cpp
    src/core/scheduler.cpp
    src/core/state_store.cpp
    src/core/runtime.cpp
    src/core/subsystems/bootstrap_subsystem.cpp
    src/core/subsystems/subsystems.cpp
)

target_include_directories(ws_core PUBLIC include)

add_executable(world_sim src/main.cpp)
target_link_libraries(world_sim PRIVATE ws_core)

enable_testing()

add_executable(determinism_core_test tests/determinism_core.cpp)
target_link_libraries(determinism_core_test PRIVATE ws_core)

add_test(NAME determinism_core_test COMMAND determinism_core_test)

add_executable(state_store_engine_test tests/state_store_engine.cpp)
target_link_libraries(state_store_engine_test PRIVATE ws_core)

add_test(NAME state_store_engine_test COMMAND state_store_engine_test)

add_executable(simulation_loop_test tests/simulation_loop.cpp)
target_link_libraries(simulation_loop_test PRIVATE ws_core)

add_test(NAME simulation_loop_test COMMAND simulation_loop_test)

add_executable(subsystems_test tests/subsystems.cpp)
target_link_libraries(subsystems_test PRIVATE ws_core)

add_test(NAME subsystems_test COMMAND subsystems_test)

add_executable(interactions_test tests/interactions.cpp)
target_link_libraries(interactions_test PRIVATE ws_core)

add_test(NAME interactions_test COMMAND interactions_test)

add_executable(advanced_coupling_test tests/advanced_coupling.cpp)
target_link_libraries(advanced_coupling_test PRIVATE ws_core)

add_test(NAME advanced_coupling_test COMMAND advanced_coupling_test)
