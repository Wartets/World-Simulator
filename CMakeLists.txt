cmake_minimum_required(VERSION 3.20)
project(WorldSimulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

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

add_executable(world_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/runtime_shell.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/profile_store.cpp
)
target_link_libraries(world_sim PRIVATE ws_core)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw imgui)

add_library(imgui_backend STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_backend
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_backend PUBLIC glfw)
target_compile_definitions(imgui_backend PUBLIC IMGUI_ENABLE_DOCKING)

add_executable(world_sim_gui WIN32
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main_window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main_window/color_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main_window/detail_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/display_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/session_manager/session_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/runtime_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/theme_bootstrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/ui_components.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/profile_store.cpp
)

find_package(OpenGL REQUIRED)

target_include_directories(world_sim_gui PRIVATE include)
target_link_libraries(world_sim_gui PRIVATE ws_core imgui_backend OpenGL::GL glfw)
target_compile_definitions(world_sim_gui PRIVATE IMGUI_ENABLE_DOCKING)

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

option(WS_ENABLE_EXTENDED_LEGACY_TESTS "Enable extended legacy integration tests" OFF)
if(WS_ENABLE_EXTENDED_LEGACY_TESTS)
    ws_add_test(advanced_coupling_test tests/advanced_coupling.cpp)
    ws_add_test(control_replay_test tests/control_replay.cpp)
    ws_add_test(parallel_stability_test tests/parallel_stability.cpp)
endif()
