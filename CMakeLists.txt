cmake_minimum_required(VERSION 3.20)
project(WorldSimulator VERSION 0.1.1 LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

# Some third-party dependencies still ship with legacy CMake files that
# declare a minimum version older than the policy baseline required by
# modern CMake releases. Keep the build permissive for those subprojects
# without lowering the project-wide minimum.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version accepted by nested projects" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WS_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis (uses .clang-tidy when available)" OFF)
if(WS_ENABLE_CLANG_TIDY)
    find_program(WS_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
    if(WS_CLANG_TIDY_EXECUTABLE)
        set(CMAKE_CXX_CLANG_TIDY "${WS_CLANG_TIDY_EXECUTABLE}")
        message(STATUS "WS_ENABLE_CLANG_TIDY is ON: using ${WS_CLANG_TIDY_EXECUTABLE}")
    else()
        message(WARNING "WS_ENABLE_CLANG_TIDY is ON, but clang-tidy was not found. Continuing without clang-tidy.")
    endif()
endif()

include(FetchContent)

set(WS_GUI_ICON_RC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/world_simulator_icon.rc")
set(WS_GUI_MANIFEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/world_simulator.manifest")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/world_simulator_version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/world_simulator_version.rc
    @ONLY
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/world_simulator.manifest.in")
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/world_simulator.manifest.in
        ${CMAKE_CURRENT_BINARY_DIR}/world_simulator.manifest
        @ONLY
    )
    set(WS_GUI_MANIFEST_FILE "${CMAKE_CURRENT_BINARY_DIR}/world_simulator.manifest")
endif()

set(WS_GUI_ICON_ID_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/world_simulator_icon_id.h")
set(WS_GUI_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/world_simulator.ico")
set(WS_GUI_VERSION_RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/world_simulator_version.rc")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/world_simulator_icon.rc.in")
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/world_simulator_icon.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/world_simulator_icon.rc
        @ONLY
    )
    set(WS_GUI_ICON_RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/world_simulator_icon.rc")
endif()

option(WS_ENABLE_OPENMP "Enable OpenMP acceleration for eligible simulation loops" OFF)
option(WS_ENABLE_GEOTIFF "Enable GeoTIFF import support (requires GDAL)" OFF)
option(WS_ENABLE_NETCDF "Enable NetCDF import support (requires NetCDF C++)" OFF)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/replay_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/checkpoint_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/run_signature.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/state_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/field_resolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/subsystems/bootstrap_subsystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/subsystems/subsystems.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/model_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/ir_ast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/initialization_binding.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/initialization_strategy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/probe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/unit_system.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/time_integrator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/spatial_scheme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/vectorized_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/neighborhood.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/random.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/multidim_support.cpp
    ${BISON_IRParser_OUTPUTS}
    ${FLEX_IRScanner_OUTPUTS}
    ${CMAKE_CURRENT_BINARY_DIR}/model_schema_generated.h
)

target_include_directories(ws_core PUBLIC
    include
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_definitions(ws_core PUBLIC WS_PROJECT_VERSION="${PROJECT_VERSION}")

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

if(WS_ENABLE_GEOTIFF)
    find_package(GDAL)
    if(GDAL_FOUND)
        target_link_libraries(ws_core PUBLIC GDAL::GDAL)
        target_compile_definitions(ws_core PUBLIC WS_ENABLE_GEOTIFF=1)
        message(STATUS "GeoTIFF support enabled (GDAL found)")
    else()
        message(WARNING "WS_ENABLE_GEOTIFF was requested, but GDAL was not found. Building without GeoTIFF support.")
        target_compile_definitions(ws_core PUBLIC WS_ENABLE_GEOTIFF=0)
    endif()
else()
    target_compile_definitions(ws_core PUBLIC WS_ENABLE_GEOTIFF=0)
endif()

if(WS_ENABLE_NETCDF)
    find_package(netCDF)
    if(netCDF_FOUND)
        target_link_libraries(ws_core PUBLIC netCDF::netcdf_cxx4)
        target_compile_definitions(ws_core PUBLIC WS_ENABLE_NETCDF=1)
        message(STATUS "NetCDF support enabled (NetCDF C++ found)")
    else()
        message(WARNING "WS_ENABLE_NETCDF was requested, but NetCDF C++ was not found. Building without NetCDF support.")
        target_compile_definitions(ws_core PUBLIC WS_ENABLE_NETCDF=0)
    endif()
else()
    target_compile_definitions(ws_core PUBLIC WS_ENABLE_NETCDF=0)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_storage.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/crash_report.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main_window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main_window/color_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main_window/detail_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main_window/window_state_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main_window/session_wizard_helpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main_window/platform_dialogs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/main_window/overlay_rendering.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/presentation/display_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/session_manager/session_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/runtime/runtime_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/platform_display_scale.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/storage_paths.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/launch_options.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/parameter_panel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/perturbation_panel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/event_logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/timeseries_panel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/histogram_panel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/constraint_monitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/time_control_panel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/theme_bootstrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/theme_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/keyboard_shortcuts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/accessibility_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/ui_components.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/node_editor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/model_editor_window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/property_inspector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/model_validator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/model_selector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/model_history.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/world_selector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/world_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/generation_advisor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/paint_tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/world_viewport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/presentation/heatmap_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/presentation/viewport_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/presentation/render_rule_editor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/presentation/vector_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/presentation/contour_renderer.cpp
    ${WS_GUI_ICON_RC_FILE}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/profile_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/world_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/noise_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/data_importer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_storage.cpp
)

find_package(OpenGL REQUIRED)

target_include_directories(world_sim_gui PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(world_sim_gui PRIVATE ws_core imgui_backend OpenGL::GL glfw)
target_compile_definitions(world_sim_gui PRIVATE IMGUI_ENABLE_DOCKING)

if(MSVC)
    target_compile_options(world_sim PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_compile_options(world_sim_gui PRIVATE $<$<CONFIG:Release>:/Zi>)

    target_link_options(world_sim PRIVATE
        $<$<CONFIG:Release>:/DEBUG:FULL /INCREMENTAL:NO /OPT:REF /OPT:ICF>
        $<$<CONFIG:RelWithDebInfo>:/DEBUG:FULL>)
    target_link_options(world_sim_gui PRIVATE
        $<$<CONFIG:Release>:/DEBUG:FULL /INCREMENTAL:NO /OPT:REF /OPT:ICF>
        $<$<CONFIG:RelWithDebInfo>:/DEBUG:FULL>)
endif()

if(NOT MSVC)
    find_program(WS_OBJCOPY_EXECUTABLE NAMES llvm-objcopy objcopy)
    if(WS_OBJCOPY_EXECUTABLE)
        add_custom_command(TARGET world_sim POST_BUILD
            COMMAND ${WS_OBJCOPY_EXECUTABLE} --only-keep-debug
                $<TARGET_FILE:world_sim>
                $<TARGET_FILE:world_sim>.debug
            VERBATIM)
        add_custom_command(TARGET world_sim_gui POST_BUILD
            COMMAND ${WS_OBJCOPY_EXECUTABLE} --only-keep-debug
                $<TARGET_FILE:world_sim_gui>
                $<TARGET_FILE:world_sim_gui>.debug
            VERBATIM)
    endif()
endif()

include(GNUInstallDirs)

install(TARGETS world_sim world_sim_gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(MSVC)
    install(FILES
        $<TARGET_PDB_FILE:world_sim>
        $<TARGET_PDB_FILE:world_sim_gui>
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        CONFIGURATIONS Release RelWithDebInfo
        OPTIONAL)
endif()

if(NOT MSVC)
    install(FILES
        $<TARGET_FILE:world_sim>.debug
        $<TARGET_FILE:world_sim_gui>.debug
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        CONFIGURATIONS Release RelWithDebInfo
        OPTIONAL)
endif()

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    DESTINATION .
)

install(DIRECTORY
    ${CMAKE_CURRENT_SOURCE_DIR}/models/
    DESTINATION models
)

set(CPACK_PACKAGE_NAME "WorldSimulator")
set(CPACK_PACKAGE_VENDOR "World-Simulator")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "World Simulator")
set(CPACK_PACKAGE_CONTACT "https://github.com/wartets/World-Simulator")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_MONOLITHIC_INSTALL ON)
set(CPACK_PACKAGE_EXECUTABLES
    "world_sim_gui" "World Simulator GUI"
    "world_sim" "World Simulator CLI"
)

set(wsCpackGenerators "ZIP")

if(WIN32)
    find_program(WS_MAKENSIS_EXECUTABLE NAMES makensis)
    if(WS_MAKENSIS_EXECUTABLE)
        list(APPEND wsCpackGenerators "NSIS")
        set(CPACK_NSIS_EXECUTABLE "${WS_MAKENSIS_EXECUTABLE}")
        set(CPACK_NSIS_DISPLAY_NAME "World Simulator")
        set(CPACK_NSIS_PACKAGE_NAME "World Simulator")
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
        set(CPACK_NSIS_MODIFY_PATH OFF)
        set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/world_simulator.ico")
        set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/world_simulator.ico")
        set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
    WriteRegStr HKCU "Software\Classes\.simmodel" "" "WorldSimulator.simmodel"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.simmodel" "" "World Simulator Model"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.simmodel\DefaultIcon" "" "$INSTDIR\bin\world_sim_gui.exe,0"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.simmodel\shell\open\command" "" "$\"$INSTDIR\bin\world_sim_gui.exe$\" $\"%1$\""
    WriteRegStr HKCU "Software\Classes\.wscp" "" "WorldSimulator.wscp"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.wscp" "" "World Simulator Checkpoint"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.wscp\DefaultIcon" "" "$INSTDIR\bin\world_sim_gui.exe,0"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.wscp\shell\open\command" "" "$\"$INSTDIR\bin\world_sim_gui.exe$\" $\"%1$\""
    WriteRegStr HKCU "Software\Classes\.wsexp" "" "WorldSimulator.wsexp"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.wsexp" "" "World Simulator World Export"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.wsexp\DefaultIcon" "" "$INSTDIR\bin\world_sim_gui.exe,0"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.wsexp\shell\open\command" "" "$\"$INSTDIR\bin\world_sim_gui.exe$\" $\"%1$\""
    WriteRegStr HKCU "Software\Classes\.wsworld" "" "WorldSimulator.wsworld"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.wsworld" "" "World Simulator World"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.wsworld\DefaultIcon" "" "$INSTDIR\bin\world_sim_gui.exe,0"
    WriteRegStr HKCU "Software\Classes\WorldSimulator.wsworld\shell\open\command" "" "$\"$INSTDIR\bin\world_sim_gui.exe$\" $\"%1$\""
System::Call 'shell32::SHChangeNotify(i, i, p, p) (0x08000000, 0, 0, 0)'
]=])
        set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
    ReadRegStr $0 HKCU "Software\Classes\.simmodel" ""
StrCmp $0 "WorldSimulator.simmodel" 0 +2
    DeleteRegKey HKCU "Software\Classes\.simmodel"
    DeleteRegKey HKCU "Software\Classes\WorldSimulator.simmodel"
    ReadRegStr $0 HKCU "Software\Classes\.wscp" ""
StrCmp $0 "WorldSimulator.wscp" 0 +2
    DeleteRegKey HKCU "Software\Classes\.wscp"
    DeleteRegKey HKCU "Software\Classes\WorldSimulator.wscp"
    ReadRegStr $0 HKCU "Software\Classes\.wsexp" ""
StrCmp $0 "WorldSimulator.wsexp" 0 +2
    DeleteRegKey HKCU "Software\Classes\.wsexp"
    DeleteRegKey HKCU "Software\Classes\WorldSimulator.wsexp"
    ReadRegStr $0 HKCU "Software\Classes\.wsworld" ""
StrCmp $0 "WorldSimulator.wsworld" 0 +2
    DeleteRegKey HKCU "Software\Classes\.wsworld"
    DeleteRegKey HKCU "Software\Classes\WorldSimulator.wsworld"
System::Call 'shell32::SHChangeNotify(i, i, p, p) (0x08000000, 0, 0, 0)'
]=])
    endif()
endif()

list(JOIN wsCpackGenerators ";" CPACK_GENERATOR)

include(CPack)

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
ws_add_test(time_integrator_test tests/time_integrator.cpp)
ws_add_test(runtime_integrator_switch_test tests/runtime_integrator_switch.cpp)
ws_add_test(integration_multi_model_test tests/integration_multi_model.cpp)
ws_add_test(conway_activity_test tests/conway_activity.cpp)
ws_add_test(full_pipeline_multi_model_test tests/full_pipeline_multi_model.cpp)
ws_add_test(ui_error_paths_multi_model_test tests/ui_error_paths_multi_model.cpp)
ws_add_test(ui_state_transitions_multi_model_test tests/ui_state_transitions_multi_model.cpp)
ws_add_test(shell_support_test tests/shell_support.cpp)
ws_add_test(live_patching_test tests/live_patching.cpp)
ws_add_test(error_context_test tests/error_context_test.cpp)
ws_add_test(profile_store_test tests/profile_store.cpp)
ws_add_test(model_history_test tests/model_history.cpp)
ws_add_test(performance_optimization_test tests/performance_optimization.cpp)
ws_add_test(advanced_features_test tests/advanced_features.cpp)
ws_add_test(noise_generator_test tests/noise_generator_test.cpp)
ws_add_test(data_importer_test tests/data_importer_test.cpp)
ws_add_test(shader_editor_test tests/shader_editor_test.cpp)
ws_add_test(checkpoint_variable_cadence_test tests/checkpoint_variable_cadence.cpp)
ws_add_test(onboarding_tutorial_test tests/onboarding_tutorial_test.cpp)
target_sources(live_patching_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
)
target_sources(shell_support_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
)
target_sources(runtime_integrator_switch_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
)
target_sources(profile_store_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/profile_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
)
target_sources(model_history_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/model_history.cpp
)
target_sources(noise_generator_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/noise_generator.cpp
)
target_sources(data_importer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/data_importer.cpp
)
target_sources(shader_editor_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/shader_editor.cpp
)

add_executable(viewport_manager_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/viewport_manager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/presentation/viewport_manager.cpp"
)
target_link_libraries(viewport_manager_test PRIVATE ws_core)
add_test(NAME viewport_manager_test COMMAND viewport_manager_test)

add_executable(display_manager_validation_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/display_manager_validation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/presentation/display_manager.cpp"
)
target_link_libraries(display_manager_validation_test PRIVATE ws_core)
add_test(NAME display_manager_validation_test COMMAND display_manager_validation_test)

add_executable(status_message_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/status_message_test.cpp"
)
target_link_libraries(status_message_test PRIVATE ws_core)
add_test(NAME status_message_test COMMAND status_message_test)

add_executable(model_validator_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/model_validator_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/model_validator.cpp"
)
target_link_libraries(model_validator_test PRIVATE ws_core)
add_test(NAME model_validator_test COMMAND model_validator_test)

add_executable(exception_message_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/exception_message_test.cpp"
)
target_link_libraries(exception_message_test PRIVATE ws_core)
add_test(NAME exception_message_test COMMAND exception_message_test)

add_executable(message_formatter_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/message_formatter_test.cpp"
)
target_link_libraries(message_formatter_test PRIVATE ws_core)
add_test(NAME message_formatter_test COMMAND message_formatter_test)

add_executable(launch_options_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/launch_options_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/launch_options.cpp"
)
target_link_libraries(launch_options_test PRIVATE ws_core)
add_test(NAME launch_options_test COMMAND launch_options_test)

add_executable(crash_report_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/crash_report_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/crash_report.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/storage_paths.cpp"
)
target_link_libraries(crash_report_test PRIVATE ws_core)
add_test(NAME crash_report_test COMMAND crash_report_test)

target_sources(full_pipeline_multi_model_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/runtime/runtime_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/presentation/display_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/profile_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/world_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_storage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
)
target_sources(conway_activity_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/runtime/runtime_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/profile_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/world_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_storage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
)
target_sources(ui_error_paths_multi_model_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/runtime/runtime_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/profile_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/world_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_storage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
)
target_sources(ui_state_transitions_multi_model_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/runtime/runtime_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/shell_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/profile_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/world_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_storage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
)
ws_add_test(time_control_scrubbing_test tests/time_control_scrubbing.cpp)
target_sources(time_control_scrubbing_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/checkpoint_storage.cpp
)

option(WS_ENABLE_EXTENDED_LEGACY_TESTS "Enable extended legacy integration tests" OFF)
if(WS_ENABLE_EXTENDED_LEGACY_TESTS)
    ws_add_test(advanced_coupling_test tests/advanced_coupling.cpp)
    ws_add_test(control_replay_test tests/control_replay.cpp)
    ws_add_test(parallel_stability_test tests/parallel_stability.cpp)
endif()
