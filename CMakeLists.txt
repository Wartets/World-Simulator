cmake_minimum_required(VERSION 3.20)
project(WorldSimulator VERSION 0.1.0 LANGUAGES CXX)

# Some third-party dependencies still ship with legacy CMake files that
# declare a minimum version older than the policy baseline required by
# modern CMake releases. Keep the build permissive for those subprojects
# without lowering the project-wide minimum.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version accepted by nested projects" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

option(WS_ENABLE_OPENMP "Enable OpenMP acceleration for eligible simulation loops" OFF)

# --- Lexer & Parser Generation ---
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

BISON_TARGET(IRParser src/core/ir.y ${CMAKE_CURRENT_BINARY_DIR}/ir_parser.cpp)
FLEX_TARGET(IRScanner src/core/ir.l ${CMAKE_CURRENT_BINARY_DIR}/ir_lexer.cpp)
ADD_FLEX_BISON_DEPENDENCY(IRScanner IRParser)

# --- FlatBuffers Schema Setup ---
# The target `flatc` will be built from sources fetched via FetchContent.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/model_schema_generated.h
    COMMAND flatc -c -o ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src/core/model_schema.fbs
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/model_schema.fbs flatc
    COMMENT "Building FlatBuffers schema (model_schema_generated.h)"
)

add_library(ws_core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/profile_resolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/interactions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/observability.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/event_queue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/replay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/run_signature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/state_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/subsystems/bootstrap_subsystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/subsystems/subsystems.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/model_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/ir_ast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/probe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/unit_system.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/time_integrator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/spatial_scheme.cpp
    ${BISON_IRParser_OUTPUTS}
    ${FLEX_IRScanner_OUTPUTS}
    ${CMAKE_CURRENT_BINARY_DIR}/model_schema_generated.h
)

target_include_directories(ws_core PUBLIC
    include
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(WS_ENABLE_OPENMP)
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(ws_core PUBLIC OpenMP::OpenMP_CXX)
        target_compile_definitions(ws_core PUBLIC WS_ENABLE_OPENMP=1)
    else()
        message(WARNING "WS_ENABLE_OPENMP was requested, but OpenMP was not found. Building without OpenMP acceleration.")
        target_compile_definitions(ws_core PUBLIC WS_ENABLE_OPENMP=0)
    endif()
else()
    target_compile_definitions(ws_core PUBLIC WS_ENABLE_OPENMP=0)
endif()

# --- Dependencies Fetching ---
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
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG v23.5.26
)
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.0.2
)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw imgui nlohmann_json flatbuffers miniz)

target_link_libraries(ws_core PUBLIC
    nlohmann_json::nlohmann_json
    flatbuffers
    miniz
)

add_executable(world_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/runtime_shell.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/profile_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/world_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/noise_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/data_importer.cpp
)
target_link_libraries(world_sim PRIVATE ws_core)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/parameter_panel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/perturbation_panel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/event_logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/timeseries_panel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/histogram_panel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/constraint_monitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/theme_bootstrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/ui_components.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/node_editor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/model_editor_window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/property_inspector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/model_validator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/model_selector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/model_history.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/world_selector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/world_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/paint_tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/world_viewport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/heatmap_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/viewport_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/render_rule_editor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/vector_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/contour_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/profile_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/world_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/noise_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/data_importer.cpp
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
ws_add_test(model_parser_test tests/model_parser_test.cpp)
ws_add_test(live_patching_test tests/live_patching.cpp)
target_sources(live_patching_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
)

option(WS_ENABLE_EXTENDED_LEGACY_TESTS "Enable extended legacy integration tests" OFF)
if(WS_ENABLE_EXTENDED_LEGACY_TESTS)
    ws_add_test(advanced_coupling_test tests/advanced_coupling.cpp)
    ws_add_test(control_replay_test tests/control_replay.cpp)
    ws_add_test(parallel_stability_test tests/parallel_stability.cpp)
endif()
