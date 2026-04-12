# World-Simulator Developer Guide

## Table of Contents
1. [Architecture Overview](#architecture-overview)
2. [Core Engine](#core-engine)
3. [Extending with Custom Subsystems](#extending-with-custom-subsystems)
4. [GPU Acceleration](#gpu-acceleration)
5. [Contribution Guidelines](#contribution-guidelines)
6. [Performance Tuning](#performance-tuning)
7. [Testing Strategy](#testing-strategy)
8. [Model Editor and Validation](#model-editor-and-validation)
9. [Rendering and Shader Rules](#rendering-and-shader-rules)


## Architecture Overview

### Design Philosophy

World-Simulator follows a **modular, deterministic simulation architecture**:

- **Core separation:** Physics simulation decoupled from UI/visualization
- **Determinism first:** All computations are reproducible given the same seed and parameters
- **SIMD-friendly:** Memory layout (Structure of Arrays) enables vectorization
- **Extensible:** Custom subsystems and models can be added without core changes
- **Observable:** Full checkpoint/restore and detailed event logging

### Directory Structure

```
src/
├── core/                                   # Core simulation engine
│   ├── runtime.cpp                         # Runtime lifecycle and deterministic step dispatch
│   ├── scheduler.cpp                       # Subsystem DAG scheduling and execution diagnostics
│   ├── state_store.cpp                     # SoA field storage, snapshots, hashing, boundary access
│   ├── interactions.cpp                    # Interaction definitions and runtime event application
│   ├── event_queue.cpp                     # Queued interaction event storage and ordering
│   ├── observability.cpp                   # Runtime counters, traces, and state metrics
│   ├── replay.cpp                          # Replay model and replay compatibility behavior
│   ├── replay_engine.cpp                   # Replay execution driver and synchronization logic
│   ├── checkpoint_manager.cpp              # In-memory checkpoint capture/restore management
│   ├── run_signature.cpp                   # Model/profile/grid signature hashing and identity checks
│   ├── field_resolver.cpp                  # Field lookup, alias resolution, and access helpers
│   ├── profile_resolver.cpp                # Model profile resolution and compatibility assumptions
│   ├── model_parser.cpp                    # `.simmodel` parsing and model execution spec extraction
│   ├── ir_ast.cpp                          # IR AST representation and helper transforms
│   ├── initialization_binding.cpp          # World initialization binding to model variables
│   ├── initialization_strategy.cpp         # Initialization algorithms and strategy orchestration
│   ├── probe.cpp                           # Probe definitions and runtime sample collection
│   ├── unit_system.cpp                     # Unit expression parsing and normalization support
│   ├── unit_lint.cpp                       # Derived-unit alias detection and recommendations
│   ├── time_integrator.cpp                 # Integrator implementations and registry wiring
│   ├── spatial_scheme.cpp                  # Spatial discretization scheme selection and utilities
│   ├── vectorized_ops.cpp                  # SIMD-friendly numeric helper operations
│   ├── neighborhood.cpp                    # Neighborhood stencils and boundary handlers
│   ├── multidim_support.cpp                # N-dimensional indexing and boundary resolver support
│   ├── random.cpp                          # Deterministic RNG primitives for reproducible runs
│   ├── ir.l                                # Flex lexer definition for IR parsing
│   ├── ir.y                                # Bison parser grammar for IR parsing
│   └── subsystems/                         # Built-in subsystem implementations
│       ├── bootstrap_subsystem.cpp         # Bootstrap/initialization subsystem wiring
│       └── subsystems.cpp                  # Built-in subsystem catalog and factory helpers
├── app/                                    # Application and persistence layer
│   ├── runtime_shell.cpp                   # Interactive CLI runtime shell implementation
│   ├── shell_support.cpp                   # Shell parsing utilities, presets, and shared helpers
│   ├── checkpoint_io.cpp                   # Checkpoint binary serialization/deserialization
│   ├── checkpoint_storage.cpp              # Timeline checkpoint persistence and retention policy
│   ├── profile_store.cpp                   # Profile persistence and retrieval
│   ├── world_store.cpp                     # Saved world metadata and world state storage
│   ├── noise_generator.cpp                 # Procedural noise generation for world initialization
│   └── data_importer.cpp                   # CSV/image/GeoTIFF/NetCDF import and normalization
├── gui/                                    # ImGui-based GUI application
│   ├── main.cpp                            # GUI entry point and window/bootstrap orchestration
│   ├── main_window.cpp                     # Top-level screen flow and UI orchestration
│   ├── main_window/                        # Main window helper modules
│   │   ├── color_utils.cpp                 # Color conversion and display color utilities
│   │   ├── detail_utils.cpp                # Detail panel utility helpers
│   │   ├── window_state_store.cpp          # Window/UI state persistence helpers
│   │   ├── session_wizard_helpers.cpp      # New session/world wizard helper behavior
│   │   ├── platform_dialogs.cpp            # Platform file dialog abstraction
│   │   ├── overlay_rendering.cpp           # Overlay primitives and shared overlay rendering
│   │   └── impl/                           # Inlined main-window section implementations
│   ├── runtime/                            # Runtime/session bridge layer
│   │   └── runtime_service.cpp             # GUI-facing runtime service and session control API
│   ├── session_manager/                    # Session/world/model lifecycle orchestration
│   │   └── session_manager.cpp             # Session manager implementation
│   ├── presentation/                       # Rendering and viewport presentation modules
│   │   ├── heatmap_renderer.cpp            # Scalar heatmap rendering pipeline
│   │   ├── vector_renderer.cpp             # Vector field rendering pipeline
│   │   ├── contour_renderer.cpp            # Contour rendering pipeline
│   │   ├── viewport_manager.cpp            # Viewport state, camera, and projection management
│   │   ├── display_manager.cpp             # Display composition and panel coordination
│   │   └── render_rule_editor.cpp          # Rule-based render customization UI
│   ├── model_selector.cpp                  # Model selection UI and model catalog interactions
│   ├── world_selector.cpp                  # World selection UI and world catalog interactions
│   ├── world_generator.cpp                 # World generation wizard and setup flow
│   ├── generation_advisor.cpp              # Generation advisories and preflight analysis
│   ├── world_viewport.cpp                  # Main simulation viewport behavior
│   ├── paint_tools.cpp                     # Paint/brush and selection editing tools
│   ├── parameter_panel.cpp                 # Runtime parameter control panel
│   ├── perturbation_panel.cpp              # Runtime perturbation/event injection panel
│   ├── event_logger.cpp                    # Runtime event log UI and navigation
│   ├── timeseries_panel.cpp                # Probe time-series visualization panel
│   ├── histogram_panel.cpp                 # Histogram/statistics panel
│   ├── constraint_monitor.cpp              # Runtime constraint and violation monitor
│   ├── time_control_panel.cpp              # Playback, scrubbing, and timeline controls
│   ├── model_editor_window.cpp             # Model editor main window and workflow
│   ├── node_editor.cpp                     # Node graph editing behavior
│   ├── property_inspector.cpp              # Model node/property inspection UI
│   ├── model_validator.cpp                 # Model validation wiring and diagnostic surfacing
│   ├── model_history.cpp                   # Undo/redo and model history operations
│   ├── shader_editor.cpp                   # GLSL shader editing/validation support
│   ├── keyboard_shortcuts.cpp              # Global shortcut registry and dispatch
│   ├── accessibility_manager.cpp           # Accessibility helpers and UI adjustments
│   ├── theme_manager.cpp                   # Theme loading, persistence, and runtime switching
│   ├── theme_bootstrap.cpp                 # Theme bootstrap/default theme initialization
│   ├── launch_options.cpp                  # GUI launch argument parsing and routing
│   ├── storage_paths.cpp                   # Cross-platform user storage path resolution
│   ├── platform_display_scale.cpp          # DPI/display scale handling
│   ├── crash_report.cpp                    # Crash capture and report generation
│   ├── ui_components.cpp                   # Shared reusable UI widgets/components
│   ├── resources/                          # Icons, manifests, and Windows resource definitions
│   └── shaders/                            # Built-in shader assets used by renderers
└── main.cpp                                # CLI executable entry point (runtime shell)

include/ws/
├── core/                                   # Core public headers
│   ├── runtime.hpp                         # Runtime API and runtime configuration types
│   ├── scheduler.hpp                       # Scheduler, subsystem interface, diagnostics contracts
│   ├── state_store.hpp                     # StateStore API, snapshots, and memory layout contracts
│   ├── profile.hpp                         # Model profile and profile resolver contracts
│   ├── time_integrator.hpp                 # Integrator API and registry definitions
│   ├── interactions.hpp                    # Interaction data structures and APIs
│   ├── neighborhood.hpp                    # Neighborhood stencil and boundary handling APIs
│   ├── random.hpp                          # Deterministic RNG API
│   ├── multidim_support.hpp                # Multi-dimensional grid/index utilities
│   ├── model_parser.hpp                    # Model parsing and execution spec API
│   ├── replay.hpp                          # Replay model public API
│   ├── replay_engine.hpp                   # Replay execution API
│   ├── checkpoint_manager.hpp              # In-memory checkpoint management API
│   ├── run_signature.hpp                   # Run signature and identity hash API
│   ├── unit_system.hpp                     # Unit-system and parsing API
│   ├── unit_lint.hpp                       # Unit alias lint API
│   ├── spatial_scheme.hpp                  # Spatial scheme API
│   ├── vectorized_ops.hpp                  # Vectorized helper API
│   ├── probe.hpp                           # Probe API
│   ├── observability.hpp                   # Runtime observability API
│   ├── event_queue.hpp                     # Event queue API
│   ├── field_resolver.hpp                  # Field resolver API
│   ├── initialization_binding.hpp          # Initialization binding API
│   └── initialization_strategy.hpp         # Initialization strategy API
├── app/                                    # App-layer public headers
│   ├── runtime_shell.hpp                   # Runtime shell public entry point
│   ├── shell_support.hpp                   # Shared shell support helpers
│   ├── checkpoint_io.hpp                   # Checkpoint file I/O API
│   ├── checkpoint_storage.hpp              # Checkpoint storage policy/index API
│   ├── profile_store.hpp                   # Profile persistence API
│   ├── world_store.hpp                     # World persistence API
│   ├── noise_generator.hpp                 # Noise generation API
│   └── data_importer.hpp                   # Data import and validation API
└── gui/                                    # GUI public headers
    ├── main_window.hpp                     # Main window API
    ├── runtime_service.hpp                 # Runtime service API for GUI orchestration
    ├── model_validator.hpp                 # Model validator API
    ├── onboarding_tutorial.hpp             # Onboarding tutorial flow definitions
    ├── shader_editor.hpp                   # Shader editor and validator API
    ├── launch_options.hpp                  # GUI launch parsing API
    └── session_manager/                    # Session manager public interfaces

tests/                                      # Repository test suite
├── determinism_core.cpp                    # Deterministic stepping and replay invariants
├── simulation_loop.cpp                     # Runtime stepping loop and control behavior
├── state_store_engine.cpp                  # StateStore allocation, access, and snapshot behavior
├── subsystems.cpp                          # Built-in subsystem behavior tests
├── interactions.cpp                        # Interaction and event application tests
├── model_parser_test.cpp                   # Model parsing and package round-trip tests
├── model_validator_test.cpp                # Model validator diagnostics and dependency tests
├── onboarding_tutorial_test.cpp            # Onboarding tutorial progression tests
├── checkpoint_variable_cadence.cpp         # Per-variable checkpoint cadence policy tests
├── live_patching.cpp                       # Runtime live patching behavior tests
├── profile_store.cpp                       # Profile persistence behavior tests
├── control_replay.cpp                      # Replay control and resume behavior tests
├── time_control_scrubbing.cpp              # Timeline scrubbing and jump behavior tests
├── advanced_features.cpp                   # Cross-feature and advanced behavior tests
├── advanced_coupling.cpp                   # Coupled model/interaction behavior tests
├── integration_multi_model.cpp             # Multi-model integration coverage
└── full_pipeline_multi_model.cpp           # End-to-end full pipeline coverage
```

### GUI ownership map

- `src/gui/runtime/`: runtime orchestration boundary between GUI actions and core simulation service calls.
- `src/gui/presentation/`: display assembly and viewport rendering support.
- `src/gui/session_manager/`: session/profile lifecycle and persistence-oriented orchestration.
- `src/gui/main_window.cpp` and `src/gui/main_window/`: top-level wiring and screen-flow coordination.


## Core Engine

### Runtime Contract

The runtime is the heart of the project. It resolves a model profile, initializes the state store, and advances the simulation using a fixed step contract.

```cpp
class Runtime {
public:
    void initialize();
    void step();
    void loadCheckpoint(const Checkpoint& cp);
    Checkpoint captureCheckpoint() const;
};
```

The step contract is intentionally stable and must remain in the following order:

1. Apply input patches.
2. Process queued interaction events.
3. Execute the scheduler.
4. Commit state metadata and hashes.

This ordering is critical for reproducibility. If the order changes, replay and checkpoint tests must be updated together.

### StateStore: Structure of Arrays Layout

`StateStore` is the central data container. It stores scalar fields in a structure-of-arrays layout so that hot loops remain cache-friendly and deterministic.

The public API includes the following concepts from `include/ws/core/state_store.hpp`:

- `StateHeader` for step index and runtime status.
- `MemoryLayout` for layout introspection.
- `StateStoreSnapshot` for checkpointing.
- `StateStore::WriteSession` for controlled writes.

The storage model supports:

- field allocation and lookup,
- dense scalar access,
- sparse overlays for localized edits,
- validity masks for invalid cells,
- explicit boundary handling (`Clamp`, `Wrap`, `Reflect`),
- deterministic hashing for replay checks.

SoA versus AoS:

```cpp
// AoS (bad for vectorization)
struct Cell { float temperature, pressure, humidity; };
std::vector<Cell> grid;

// SoA (preferred)
struct Grid {
    std::vector<float> temperatures;
    std::vector<float> pressures;
    std::vector<float> humidities;
};
```

### Scheduler: DAG Execution

The scheduler executes subsystems in dependency order and records diagnostics as it goes. Subsystems are expected to declare their read and write sets so that the runtime can validate ownership.

```cpp
class Scheduler {
public:
    void registerSubsystem(std::shared_ptr<ISubsystem> subsystem);
    void initialize(StateStore& stateStore, const ModelProfile& profile);
    StepDiagnostics step(StateStore& stateStore,
                         const ModelProfile& profile,
                         TemporalPolicy temporalPolicy,
                         const NumericGuardrailPolicy& guardrailPolicy,
                         std::uint64_t stepIndex);
};
```

Important scheduler invariants:

- the subsystem graph must remain a DAG,
- no two subsystems may write the same variable,
- observed data flow must match declared dependencies,
- diagnostics should remain stable across identical inputs.

### Model profile and integrators

The profile layer resolves model-specific constraints and subsystem tiers.

```cpp
struct ModelProfile {
    std::map<std::string, ModelTier, std::less<>> subsystemTiers;
    std::set<std::string, std::less<>> compatibilityAssumptions;
    std::vector<std::string> conservedVariables;
    std::vector<CrossVariableConstraint> crossVariableConstraints;
};
```

Time integration is managed by `TimeIntegratorRegistry`. The built-in integrators currently include:

- `EulerExplicit`
- `RK2Midpoint`
- `RK3Heun`
- `SemiImplicitEuler`
- `VelocityVerlet`
- `CrankNicolson`
- `RK4`

The runtime also supports alias resolution for integrator identifiers, so UI labels can be user-friendly while the underlying id remains stable.

### Checkpointing and replay

The checkpoint workflow spans core and app layers. In the app layer, `CheckpointStorage` manages retention and disk-backed timeline storage. The runtime layer uses `Checkpoint` / `StateStoreSnapshot` data to restore exact state.

Key rules:

- checkpoint saves must preserve model identity and profile fingerprint,
- replay must reproduce the same state hash for the same seed and configuration,
- a mismatch should be treated as a correctness issue, not a cosmetic warning.

---

## Extending with Custom Subsystems

### Subsystem contract

Subsystems implement `ISubsystem` and are registered with the scheduler. A subsystem should declare what it reads and writes, then perform work in a deterministic order.

```cpp
class ISubsystem {
public:
    virtual std::string name() const = 0;
    virtual std::vector<std::string> declaredReadSet() const = 0;
    virtual std::vector<std::string> declaredWriteSet() const = 0;
    virtual void initialize(const StateStore& stateStore,
                            StateStore::WriteSession& writeSession,
                            const ModelProfile& profile) = 0;
    virtual void step(const StateStore& stateStore,
                      StateStore::WriteSession& writeSession,
                      const ModelProfile& profile,
                      std::uint64_t stepIndex) = 0;
};
```

### Recommended extension workflow

1. Define the subsystem with a small, explicit responsibility.
2. Declare the read set and write set.
3. Register the subsystem in the scheduler.
4. Add regression tests for determinism and boundary behavior.
5. Document any new invariant in the relevant guide or model doc.

### Custom neighborhoods

Spatial stencils are implemented through `NeighborhoodStencil` and `BoundaryHandler`.

```cpp
#include "ws/core/neighborhood.hpp"

auto stencil = NeighborhoodStencil::createMoore8();
BoundaryHandler boundary(BoundaryHandler::Type::Periodic);
```

When a subsystem uses a custom stencil:

- keep the offset list deterministic,
- prefer small fixed neighborhoods in hot paths,
- make boundary behavior explicit,
- avoid dynamic allocation inside the cell loop.

### Deterministic randomness

Use `DeterministicRNG` from `include/ws/core/random.hpp` for any stochastic subsystem logic.

```cpp
DeterministicRNG rng(globalSeed);
for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
        rng.seedCell(x, y, currentStep);
        float noise = rng.uniform(-1.0f, 1.0f);
    }
}
```

The guiding rule is simple: the same seed plus the same config must produce the same result.

### Multi-dimensional support

The project currently ships with multidimensional helpers in `include/ws/core/multidim_support.hpp`, which include `GridDimensions`, `GridStrides`, and `MultiDimBoundaryResolver`.

Use these helpers if a subsystem needs future-ready indexing logic, but keep the default behavior aligned with the 2D runtime that ships today.

### Performance-critical APIs

For hot loops, prefer the fast scalar access functions from `StateStore` and avoid conversions or repeated lookup inside the inner loop.

```cpp
float val = state.trySampleScalarFast(handle, x, y);
state.setScalarFast(handle, x, y, value);
```

If you need to perform bulk mutation, use a write session or a dedicated initialization pass rather than ad hoc loops sprinkled across the runtime.

---

## GPU Acceleration

### CPU determinism first

Simulation logic executes on CPU so that identical inputs produce bit-exact state hashes across platforms and runs. This is not a temporary workaround; it is part of the project’s reproducibility contract.

Why this matters:

1. Scientific work needs reproducible runs.
2. Debugging is much easier when state changes are stable.
3. Checkpoints and replay depend on exact matching state transitions.
4. The test suite checks reproducibility, not just approximate similarity.

### Current rendering pipeline

The GUI uses GPU hardware for visualization through OpenGL, but that GPU usage is strictly for rendering. The current rendering pipeline includes:

- heatmap rendering,
- vector rendering,
- contour rendering,
- presentation overlays and rule-based display.

Shader editing is available in the GUI, but it is sandboxed and affects rendering only.

### Shader editor scope

The shader editor currently supports advanced visualization rules with GLSL source editing and validation.

Supported behaviors include:

- syntax checking,
- brace balancing,
- `main()` detection,
- resource binding inspection,
- live preview,
- error recovery to the last valid shader,
- code formatting for readability.

The current build supports vertex and fragment shaders only. Geometry, tessellation, and compute shaders are not part of the shipped workflow.

### GPU compute as future work

GPU compute for simulation logic remains future work. If it is ever added, it must be explicit, opt-in, and validated against the CPU baseline before it is treated as authoritative.

Any future GPU path should satisfy:

- explicit user opt-in,
- verification against CPU output,
- graceful fallback when the GPU path fails,
- documentation of any determinism relaxation.

### Contributor guidance

When working in this area, do not blur the line between rendering and simulation. A rendering change must not change the state evolution of the simulation.

---

## Contribution Guidelines

### Code style

- Use `PascalCase` for types.
- Use `camelCase` for functions, methods, variables, and parameters.
- Keep namespaces lower-case.
- Keep includes minimal and focused.
- Prefer Allman-style braces and 4-space indentation.

These rules apply to code under `src/`, `include/`, and `tests/`.

### Naming policy

The repository’s project-owned code follows these conventions:

- Types: `PascalCase`
- Enum values: `PascalCase`
- Functions and methods: `camelCase`
- Variables and parameters: `camelCase`
- Private data members: `camelCase_`
- Global/static constants: `kPascalCase`
- Namespaces: `lower_case`

Do not introduce new naming drift in adjacent code when you are already touching a file. If a file has clear legacy drift, normalize it only when the change is low-risk and narrowly scoped.

### Numeric constant policy

Avoid scattering numeric literals when they represent layout, policy, or timing choices.

- Prefer named constants for repeated thresholds.
- Keep file-local literals only when they are truly one-off or part of a well-known external API contract.
- Use `include/ws/gui/layout_constants.hpp` or nearby constants when the same values are shared across GUI modules.

### Documentation expectations

When behavior changes, update the corresponding developer-facing document in the same change.

- If runtime semantics change, update this guide and the user guide.
- If the model format changes, update `model_format_doc.md`.
- If user-facing GUI behavior changes, update the user guide and relevant section here.

### Testing expectations

Every meaningful change should include tests that cover the updated behavior. At minimum, check:

1. determinism or replay impact,
2. correctness of the changed behavior,
3. boundary or error handling,
4. any user-visible workflow regression.

---

## Performance Tuning

### CPU-first optimization strategy

The project is tuned around deterministic CPU execution. Optimize for cache behavior and predictable access patterns before considering anything else.

Recommended practices:

- Favor structure-of-arrays over array-of-structures.
- Avoid repeated field lookup in hot loops.
- Reuse buffers instead of allocating in the step loop.
- Keep boundary handling explicit and centralized.
- Use the fast scalar access APIs for inner loops.

### Hot-path guidance

The following patterns are usually expensive:

- repeatedly resolving handles inside the cell loop,
- converting between representations during stepping,
- recomputing metadata that could be cached,
- copying large field buffers unnecessarily,
- mixing validation work into the stepping path.

### Profiling

Profile the code before broadening an optimization. If you change a hot loop, confirm that the new version is measurably better or demonstrably simpler without weakening determinism.

Useful profiling targets include:

- runtime stepping,
- state hashing,
- subsystem updates,
- rendering/presentation hot paths,
- model validation and export workflows.

### OpenMP and vectorization

Optional acceleration flags can improve eligible loops, but they do not change the baseline determinism contract. If you enable OpenMP or vectorization in a local build, validate the same test set that you would use for a normal release build.

---

## Testing Strategy

### Test categories

The repository’s test suite covers several kinds of behavior:

| Test area | Purpose |
|---|---|
| Determinism | Verify reproducibility and hash stability |
| Runtime sequencing | Confirm step order and replay behavior |
| Model parsing | Validate package parsing and round-trip behavior |
| Model validation | Check syntax, units, and dependency diagnostics |
| GUI workflow | Confirm onboarding, navigation, and state transitions |
| Import/export | Verify data importers and checkpoint storage |

### Running tests

Use the project’s CMake/CTest flow to run the appropriate suite for your change. Prefer a focused test set when you know which area you touched, then broaden to the full suite if the change affects shared runtime behavior.

### Writing determinism tests

The most important pattern is to run the same seed twice and compare the resulting hash or checkpoint identity.

```cpp
TEST(MyFeature, DeterministicReproducibility) {
    auto result1 = runSimulation(seed=42, steps=100);
    auto result2 = runSimulation(seed=42, steps=100);
    EXPECT_EQ(result1.stateHash, result2.stateHash);
}
```

If the change touches scheduling, randomness, or checkpoint behavior, add tests that confirm the state remains stable across repeated runs.

### Boundary and error tests

Add coverage for invalid field names, out-of-range coordinates, unsupported unit expressions, malformed formulas, and unsupported launch routes when relevant. A small test that clearly documents the invariant is far more useful than a large vague test that only checks that “something happened.”

### Suggested representative files

The current repository already includes targeted coverage in areas such as:

- `tests/determinism_core.cpp`
- `tests/simulation_loop.cpp`
- `tests/model_parser_test.cpp`
- `tests/model_validator_test.cpp`
- `tests/onboarding_tutorial_test.cpp`
- `tests/advanced_features.cpp`

When you add new behavior, keep the new tests near the affected subsystem or workflow so future maintenance remains obvious.

---

## Model Editor and Validation

### Model editor scope

The model editor supports graph editing, package authoring, and validation for `.simmodel` workflows. The current implementation includes:

- graph visualization,
- node and property inspection,
- structure editing,
- undo/redo history snapshots,
- packaged save/export workflows,
- package round-trip verification during save/export.

### Validation coverage

The validator currently covers:

- syntax checks for formulas,
- type checks,
- unit validation,
- dependency validation for undeclared identifiers,
- structural checks for duplicate or conflicting elements.

The validator also emits suggestions when a formula references a variable that is not declared. That should be reported as a guided authoring problem, not a mysterious failure.

### Unit policy

World-Simulator now treats explicit SI base-unit expressions as the preferred authoring form. Derived-unit aliases such as `Pa`, `N`, and `J` are acceptable as authoring shorthand only if the validator can explain how to rewrite them, and the current docs should make that clear.

Use examples like these when teaching the rule:

- preferred: `kg/(m*s^2)`
- alias with warning: `Pa`

### Onboarding tutorial

The onboarding tutorial is a six-step flow that helps users move through the project’s workflow in a structured way:

1. welcome,
2. select a model,
3. prepare a world,
4. configure the wizard,
5. run and interact,
6. persist and resume.

The tutorial is launched from the GUI and provides direct navigation to the recommended workflow state for each step.

### External data import

The app layer includes data import helpers for CSV, image, GeoTIFF, and NetCDF inputs. Imported data is resampled to the target grid and normalized to the variable domain where appropriate.

Document the current import limits clearly when you update this section:

- CSV and standard images are supported in the default build path.
- GeoTIFF and NetCDF support depend on optional build flags and external libraries.
- Imported values should be validated against the model’s declared domain.

---

## Rendering and Shader Rules

### Current state

Rendering is an application-layer concern. It is allowed to use GPU hardware for visualization, but rendering must not be described as part of the simulation engine’s baseline logic.

Current rendering features include:

- heatmap rendering,
- vector rendering,
- contour rendering,
- viewport management,
- rule-based visualization editing,
- presentation-layer overlays.

### Shader editor workflow

The GLSL shader editor is an advanced visualization tool. Its supported workflow is:

1. open the shader editor from the presentation settings panel,
2. edit GLSL source,
3. validate syntax and resource usage,
4. preview the result live,
5. revert to the last valid shader if compilation fails.

The editor should be described as sandboxed and visual only. It validates shader source, but it does not change the simulation’s step order or numerical behavior.

### Security and safety constraints

The current implementation rejects shader patterns that are unsafe or outside the editor’s intended scope.

Examples include:

- file I/O operations,
- forbidden synchronization primitives,
- excessive shader complexity,
- too many uniforms,
- unsupported shader stages in the shipped build.

### Limitations

Be explicit about the current limitations:

- current support focuses on vertex and fragment shaders,
- geometry, tessellation, and compute shaders are not shipped as part of the standard workflow,
- shader binary caching is not yet implemented,
- per-model shader persistence is not integrated,
- SPIR-V compilation targets are not available in the current build path.

### Contributor rule

Do not couple shader changes to simulation correctness. If the rendering pipeline changes, the runtime state evolution must remain unchanged for the same input seed, profile, and model.

---

**For API details, inspect the public headers under `include/ws/`. For model format details, see `model_format_doc.md`. For user-facing workflow notes, see the User Guide.**
