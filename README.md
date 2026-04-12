# World-Simulator

A deterministic, interactive grid simulation environment for model-driven 2D experiments.

## Overview

World-Simulator is a standalone, portable application for simulating complex physical and ecological systems on structured grids. The core principle is strict decoupling between simulation logic (the model package) and dynamic system state (world/profile/checkpoint data).

This separation supports reproducible experiments across machines when model identity, configuration, and seed are preserved.

### Target Audiences

- **Researchers and Scientists**: A testing environment for physical, ecological, economic, and other hypotheses
- **Educators and Students**: A pedagogical tool for interactively visualizing complex phenomena  
- **Game Developers**: A simulation backend for procedural content generation and physics visualization
- **Engineers and Modelers**: A rapid prototyping platform for computational models

---

## Features

### Core Capabilities

- **Multi-Physics Simulation**: Support for diverse simulation types including fluid dynamics, reaction-diffusion systems, ecological models, atmospheric science, urban climate, and more
- **Interactive Runtime**: Real-time parameter modification, perturbation injection, and live visualization
- **Model Editor (Experimental)**: Graph-oriented structure editor for inspection and early-stage editing workflows
- **World Generator**: Procedural terrain and state initialization using Perlin/Simplex/Worley/Wavelet noise
- **Checkpoints & Replay**: Runtime checkpoints, event logging, and replay support
- **Live Patching**: Modify simulation parameters and inject perturbations during runtime

### Technical Features

- **Strictly Typed IR**: High-performance Intermediate Representation for simulation logic
- **Domain Constraints**: Physical validity checks and automatic clamping
- **Time Integrators**: Built-in registry entries include `explicit_euler`, `rk2_midpoint`, `rk3_heun`, `semi_implicit_euler`, `velocity_verlet`, `crank_nicolson`, and `rk4` (with legacy-friendly aliases such as `Euler Explicit`, `RK2`, `Verlet`, and `RK4`); the active integrator can be switched live at runtime and is persisted in saved profiles/worlds.
- **Spatial Operators**: Laplacian, gradient, advection, diffusion with configurable schemes
- **Deterministic Execution**: Reproducible results across runs and platforms
- **Performance Architecture**: Deterministic stepping, worker-based runtime/snapshot flow, and CPU vectorization-friendly layout

### Built-in Models

The project ships with several pre-configured simulation models:

- **Conway's Game of Life** - Classic cellular automaton
- **Gray-Scott Reaction-Diffusion** - Chemical pattern formation
- **Environmental Model 2D** - Atmosphere, hydrology, and biosphere
- **Coastal Biogeochemistry** - Estuarine ecosystem and transport
- **Urban Microclimate** - City-scale thermo-hydrologic dynamics

---

## Installation

### Prerequisites

| Requirement | Minimum Version | Notes |
|-------------|:---------------:|-------|
| C++ Compiler | C++20 capable | GCC 11+, Clang 15+, MSVC 2022+ |
| CMake | 3.20+ | Build system |
| OpenGL | 3.3+ | For GUI rendering |
| GLFW | 3.4+ | Window management (fetched) |
| Git | Any recent | For version control |

### Platform Support

- **Windows 10/11** (Primary - tested)
- **Linux** (Supported)
- **macOS** (Supported, requires X11 or Cocoa)

### Build Instructions

#### 1. Clone the Repository

```bash
git clone https://github.com/wartets/World-Simulator.git
cd World-Simulator
```

#### 2. Create Build Directory

```bash
mkdir build && cd build
```

#### 3. Configure and Build

```bash
# On Windows (CMake GUI or command line)
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

# On Linux/macOS
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

#### 4. (Optional) Enable OpenMP Acceleration

For multi-threaded simulation performance:

```bash
cmake .. -DWS_ENABLE_OPENMP=ON -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

#### 5. Run the Executables

After building, you will have:

- `world_sim` - Command-line simulation runtime
- `world_sim_gui` - Interactive GUI application

#### 6. Create Distribution Packages (Installer/Archive)

From the configured build directory, package artifacts can be generated with CPack:

- **Windows**: ZIP package is always produced; NSIS installer is produced when `makensis` is available.
- **Linux/macOS**: ZIP package is produced.

Installed package layout includes:

- `bin/` executables (`world_sim_gui`, `world_sim`)
- `models/` built-in model packages
- top-level `LICENSE` and `README.md`

Release diagnostics:

- **Windows (MSVC)**: packaged builds include `.pdb` symbol files (Release/RelWithDebInfo) alongside executables.
- GUI crash diagnostics write timestamped reports under per-user settings storage:
  - Windows: `%APPDATA%/WorldSimulator/crash_reports/`
  - Linux: `${XDG_CONFIG_HOME:-~/.config}/WorldSimulator/crash_reports/`
  - macOS: `~/Library/Application Support/WorldSimulator/crash_reports/`

On Windows NSIS builds, uninstallation is provided by the generated installer and removes installed application-managed files while preserving user settings/data stored outside the install directory.

Windows NSIS builds also register per-user file associations for:

- `.simmodel` (model package)
- `.wscp` (checkpoint)
- `.wsexp` and `.wsworld` (world export/import package)

Double-click shell-open for those extensions launches `world_sim_gui` and routes through the same startup parser used by explicit CLI arguments.

---

## Quick Start

### Running the GUI

Launch the graphical interface:

```bash
# Windows
./build/Release/world_sim_gui.exe

# Linux/macOS
./build/world_sim_gui
```

### GUI Launch Arguments and File-Open Routing

The GUI executable accepts startup arguments for model/world/checkpoint workflows:

- `--model <path>`: select model scope and open Session Manager.
- `--edit-model <path>`: open model directly in Model Editor.
- `--world <name>`: open stored world by name (uses current or `--model` scope).
- `--import-world <path>`: import exported world package and open it.
- `--checkpoint <path>`: start runtime and restore a checkpoint file.
- `--open <path>` or positional `<path>`: route by extension:
  - `.simmodel` → Model Editor
  - `.wscp` → Checkpoint loader
  - `.wsexp` / `.wsworld` → World import + open

### Running from Command Line

```bash
# Run with a specific model
./build/world_sim --model models/game_of_life_model.simmodel --grid 128x128

# Run the environmental model
./build/world_sim --model models/environmental_model_2d.simmodel --steps 1000
```

### Creating a New Simulation

1. Open `world_sim_gui`
2. Select **Model Editor** from the main menu
3. Create new variables (state, parameter, derived, forcing)
4. Define interactions between variables
5. Save the model as `my_model.simmodel`
6. Generate a world using the **World Generator**
7. Run and observe the simulation

---

## Runtime Semantics and Persistence

World-Simulator has multiple persistence layers. Understanding these layers avoids accidental data loss and improves reproducibility.

### Change-scope quick guide

| Change type | Typical effect | Persisted automatically | Notes |
|---|---|---|---|
| Playback controls (play/pause/step/seek) | Immediate runtime control | No | Affects current runtime session state only |
| Runtime parameter edits / manual patches | Immediate or next-step runtime effect | No | Save world/checkpoint/event log to retain changes |
| Wizard generation settings | Applied at world creation | No | Applies to newly created world, not already-running world |
| Display/viewport preferences | UI behavior/rendering | Session/display prefs | May be stored separately from world state |
| Checkpoint creation | Snapshot of runtime state | Yes (checkpoint storage) | Supports deterministic scrubbing/replay workflows |
| Save active world | Writes world/profile data | Yes | Use before switching model/state to avoid losing in-memory changes |

### Persistence layers

- **Profile**: configuration metadata and runtime-facing settings for a world.
- **Checkpoint**: concrete state snapshot at a simulation step.
- **Saved world**: world identity plus associated persisted artifacts.
- **Event log**: intervention history for replayable operations.

Resume behavior depends on available artifacts (checkpoint-backed resume vs profile-only regeneration).

---

## Current Scope and Important Limitations

### Model Editor scope

The model editor currently provides strong structural inspection and basic graph editing, but some authoring workflows are still maturing.

- JSON snapshot export is available from the editor; full packaged `.simmodel` ZIP-style export is not available in this build.
- Save/export actions now report a package-integrity summary (for example, missing `metadata.json`, `version.json`, `ir.logic`, or `model.fb`) so JSON-only writes are explicit.
- Undo/redo restores serialized graph snapshots captured in the editor history.
- Validation includes useful checks, while deeper package-level validation remains in progress.

Use it as an experimental authoring surface and structure viewer unless your workflow is validated end-to-end for your model package.

### External data import formats

- Supported now: **CSV**, **PGM image (`P2`/`P5`)**.
- Not yet available in this build: **GeoTIFF**, **NetCDF**.

### Shortcut behavior

- `F1` is reserved for global help/shortcut reference.
- Prefer avoiding `F1` for custom viewport-local bindings to prevent context conflicts.

---

## Simulation Models

### Available Models

| Model | Description | Complexity |
|-------|-------------|:----------:|
| [Game of Life](models/game_of_life_model.simmodel/) | Conway's classic cellular automaton | Tier 1 |
| [Gray-Scott RD](models/gray_scott_reaction_diffusion.simmodel/) | Chemical reaction-diffusion patterns | Tier 2 |
| [Environmental 2D](models/environmental_model_2d.simmodel/) | Atmosphere, hydrology, biosphere | Tier 3 |
| [Coastal Biogeochemistry](models/coastal_biogeochemistry_transport.simmodel/) | Estuarine ecosystem dynamics | Tier 3 |
| [Urban Microclimate](models/urban_microclimate_resilience.simmodel/) | City-scale climate modeling | Tier 4 |

### Model Structure

Each `.simmodel` package contains:

- `model.json` - Variable definitions and stage graph
- `logic.ir` - Intermediate Representation computations
- `metadata.json` - Model metadata and author information
- `version.json` - Format and engine version compatibility

### Creating Custom Models

Define your simulation by specifying:

1. **Grid Configuration**: Dimensions, topology, boundary conditions
2. **Variables**: State, parameter, derived, and forcing types
3. **Domains**: Physical validity constraints (min/max intervals)
4. **Stages**: Ordered execution stages for one timestep
5. **Interactions**: Mathematical logic in IR format

Current runtime scope is **2D Cartesian**. Model compilation accepts `grid.dimensions` as either two explicit extents (for example `[256, 256]`) or legacy scalar `2`; higher-dimensional declarations are rejected with a validation error.

Global runtime boundary modes currently support `clamp`, `wrap`, and `reflect` (including common aliases such as `periodic`, `fixed`, `dirichlet`, `neumann`, and `reflecting`).

Example variable definition:

```json
{
  "id": "temperature",
  "role": "state",
  "support": "cell",
  "type": "f32",
  "units": "K",
  "domain": "temperature_physical"
}
```

Example interaction:

```cpp
// Simple diffusion interaction
@interaction(diffuse_temperature)
func diffuse_temperature() {
    %temp = Load("temperature", 0, 0)
    %lap = Laplacian("temperature")
    %diff_coeff = GlobalLoad("diffusivity")
    %dt = GlobalLoad("dt")
    %change = Mul(%diff_coeff, %lap)
    %delta = Mul(%change, %dt)
    Store("temperature", Add(%temp, %delta))
}
```

---

## Usage

### GUI Controls

| Action | Control |
|-------:|:--------|
| Play/Pause | Space bar or button |
| Step Forward (paused) | Right Arrow |
| Step Backward | Ctrl + Left |
| State History | Alt + Left / Alt + Right |
| Shortcut Help | F1 |
| Time Jump | Timeline slider |
| Pan View | Middle mouse drag |
| Zoom | Scroll wheel |

### Command-Line Options

```bash
world_sim [OPTIONS]

Options:
  --model <path>           Path to .simmodel file
  --world <path>           Path to world/state file
  --grid <WxH>             Grid dimensions (e.g., 256x256)
  --steps <N>              Number of steps to run
  --output <path>          Output checkpoint file
  --profile <name>         Runtime profile to use
  --verbose                Enable verbose logging
  --help                   Show this help message
```

### Parameter Modification

During runtime, you can:

- **Modify global parameters**: Double-click parameter in the panel
- **Paint cell values**: Available through viewport/world editing workflows where enabled
- **Inject perturbations**: Define forcing regions with the perturbation tool
- **Create checkpoints**: Save state at any timestep for later replay
- **Manage checkpoints**: Browse in-memory checkpoints, restore, rename, or delete them from the checkpoint panel
- **Use native file pickers**: Browse parameter presets and event logs through the OS file chooser when available
- **Run guided analysis recipes**: Use the Analysis tab to quickly set up global trend probes and checkpoint comparisons
- **Use replay preflight**: Review replayable vs skipped events, capture a baseline checkpoint, then replay compatible entries while paused

---

## Architecture

### Key Components

```
World-Simulator/
├── src/
│   ├── core/          # Simulation engine (IR, scheduler, runtime)
│   ├── app/           # High-level application (shell, storage)
│   └── gui/           # Graphical user interface
├── include/ws/        # Public API headers
├── models/            # Built-in simulation models
└── docs/              # Documentation
```

### Design Principles

1. **Model-State Separation**: Models define rules; state holds data
2. **Stage-Based Execution**: Ordered computation stages per timestep
3. **Double Buffering**: Read/write state isolation for parallelism
4. **Type Safety**: Strict typing in IR prevents runtime errors
5. **Deterministic Runtime Contract**: input patch ingestion → event queue apply → scheduler execution → state metadata/hash commit

### Technical Stack

- **Language**: C++20
- **Build System**: CMake 3.20+
- **UI Framework**: Dear ImGui
- **Graphics**: OpenGL 3.3+
- **JSON Parsing**: nlohmann/json
- **Schema**: FlatBuffers
- **Compression**: miniz

---

## Contributing

Contributions are welcome! Please read our guidelines before submitting.

### How to Contribute

1. **Fork** the repository
2. **Create** a feature branch (`git checkout -b feature/amazing-feature`)
3. **Commit** your changes (`git commit -m 'Add amazing feature'`)
4. **Push** to the branch (`git push origin feature/amazing-feature`)
5. **Open** a Pull Request

### Contribution Areas

- New simulation model implementations
- Performance optimizations
- GUI improvements and new visualizations
- Documentation improvements
- Bug fixes and test coverage
- Platform support enhancements

### Coding Standards

- Follow C++20 best practices
- Use the repository naming policy (types `PascalCase`, functions/variables `camelCase`, constants `kPascalCase`)
- Add documentation for new features
- Ensure cross-platform compatibility
- Test thoroughly before submitting

For naming lint on project-owned C++ sources, enable clang-tidy during configuration:

- `cmake .. -DWS_ENABLE_CLANG_TIDY=ON`

Identifier naming checks are configured in `.clang-tidy`.

---

## License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

---

## Acknowledgments

### Dependencies

World-Simulator uses the following open-source libraries:

- **[Dear ImGui](https://github.com/ocornut/imgui)** - Bloat-free immediate mode GUI
- **[GLFW](https://www.glfw.org/)** - Window and input management
- **[nlohmann/json](https://github.com/nlohmann/json)** - JSON parsing
- **[FlatBuffers](https://google.github.io/flatbuffers/)** - Schema-based serialization
- **[miniz](https://github.com/richgel999/miniz)** - ZIP file handling
- **[Google Test](https://google.github.io/googletest/)** - Unit testing framework
- **[CMake](https://cmake.org/)** - Build system