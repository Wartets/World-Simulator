# World-Simulator User Guide

## Table of Contents
1. [Getting Started](#getting-started)
2. [Choose a Model](#choose-a-model)
3. [Create or Resume a World](#create-or-resume-a-world)
4. [Runtime Console](#runtime-console)
5. [Persistence and Reproducibility](#persistence-and-reproducibility)
6. [Model Editor Status](#model-editor-status)
7. [Interactive Tutorial](#interactive-tutorial)
8. [External Data Import Status](#external-data-import-status)
9. [Troubleshooting](#troubleshooting)
10. [FAQ](#faq)

---

## Getting Started

### Installation

World-Simulator is a cross-platform physics simulation engine and visual editor.

**Requirements:**
- C++20 compatible compiler (GCC 10+, Clang 11+, MSVC 2019+)
- CMake 3.20+
- OpenGL 4.6+ capable graphics hardware
- 4GB RAM minimum (8GB+ recommended for large grids)

**Building from Source:**

```bash
git clone https://github.com/Wartets/World-Simulator.git
cd World-Simulator
mkdir build && cd build
cmake .. -G "Unix Makefiles" # or -G "Visual Studio 16" on Windows
cmake --build . --config Release
```

**Running the GUI:**

```bash
./world_sim_gui
```

**Running the runtime shell:**

```bash
./world_sim
```

The shell is interactive; type `help` after launch to see available commands.

### First Simulation

1. Launch `world_sim_gui`
2. Select "Environmental 2D" from the Model Selector
3. Click "New World" to create a 256×256 grid
4. Click "Play" to start the simulation
5. Observe temperature and pressure evolution in the viewport

---

## Choose a Model

Use the model selector to pick the model package first. Prioritize:

- model purpose
- last-used context
- readiness/compatibility status

Treat deep metadata (hashes and format internals) as supporting diagnostics.

---

## Create or Resume a World

### Create a world

1. Choose world name, grid size, and seed.
2. Choose a supported initialization mode.
3. Review warnings and estimated workload.
4. Create and start from paused state when you need controlled setup.

### Resume a world

- If a checkpoint exists, open resumes checkpointed runtime state.
- If no checkpoint exists, open rebuilds from saved profile settings.

Always confirm which source was used before continuing an experiment branch.
---
## Runtime Console

### Playback Controls
| Control | Keyboard | Description |
|---------|----------|-------------|
| Play/Pause | `Space` | Toggle simulation |
| Step Forward | `Ctrl+Right` | Advance one time step |
| Step Backward | `Ctrl+Left` | Go back one time step |
| Speed 0.5x | `Ctrl+1` | Halve playback speed |
| Speed 1.0x | `Ctrl+2` | Normal speed |
| Speed 2.0x | `Ctrl+3` | Double speed |
### Time Slider

Drag the timeline slider to seek through the simulation:
- Each checkpoint is marked with a dot
- Hover to see timestamp and step number
- Click to jump to that time

### Checkpoint Cadence Policies

Checkpoint timeline capture supports both global and per-variable cadence control:

- Global interval controls when checkpoint snapshots are created (for example, every 25 steps).
- Per-variable cadence controls how frequently each variable payload is refreshed inside timeline checkpoints.
- A variable cadence value of `0` freezes that variable after baseline capture (step 0).
- Unspecified variables can be either refreshed every checkpoint or frozen after baseline, based on the `include_unspecified` policy.

This policy is useful for storage budget control when some variables change slowly and do not need high-frequency persistence.

### State Inspector

Right sidebar displays:
- Current step number
- Simulation time
- Frame rate (steps/sec)
- Elapsed wall-clock time

### Display Options

**Viewport Rendering:**
- Click a variable name to display it
- Right-click to configure colormap
- Scroll wheel: Zoom
- Middle-drag: Pan

**Colormaps:**
- `Viridis`: Colorblind-friendly (default)
- `Plasma`: High contrast
- `Grayscale`: Monochrome
- `Thermal`: Red-hot theme

### Parameter Panel

Modify model parameters in real time:
1. Click "Parameters" in left sidebar
2. Adjust sliders for any model parameters
3. Save the active world/checkpoint if you want persistence beyond the current runtime session

### Event Logger

View all events and perturbations applied:
1. Click "Events" tab
2. See timestamp, event type, location, magnitude
3. Right-click to jump to that time step

---

## Persistence and Reproducibility

World-Simulator uses separate persistence layers:

- profile (configuration)
- checkpoint (state snapshot)
- saved world (identity + artifacts)
- event log (interventions)

For reproducibility, preserve model identity, seed, initialization settings, and intervention history.

### Unit expression guidance

Model files may contain practical unit expressions, but validation now flags derived-unit aliases (such as `Pa`, `N`, and `J`) and suggests SI base-unit expansions.

Preferred format for model authoring is explicit SI base units. Example:

- Preferred: `kg/(m*s^2)`
- Alias form (flagged by validator): `Pa`

---

## Model Editor Status

The model editor supports end-to-end package authoring for `.simmodel` workflows.

Current capabilities include:

- graph visualization and node/property inspection
- structure editing and history snapshots (undo/redo)
- packaged save/export workflows
- package round-trip verification during save/export
- validation coverage for syntax, units, structure, and undeclared formula dependencies

---

## Interactive Tutorial

World-Simulator includes an interactive onboarding tutorial in the GUI.

Launch options:

- Press `Shift+F1` to open the guided tutorial directly.
- Press `F1` to open shortcut help, then choose **Start guided onboarding**.

Tutorial behavior:

- Multi-step onboarding covering model selection, world setup, wizard preflight, runtime interaction, and persistence.
- Each step provides an objective and concrete guidance text.
- **Take me there** navigates to the recommended workflow state for the current step.
- **Back**, **Next**, **Restart**, and **Finish** controls support structured walkthrough progression.

---

## Shader/Render Rule Authoring Status

An experimental GLSL shader editor is available for advanced rendering customization:

**Current capabilities:**

- Fragment and vertex shader editing with syntax highlighting
- Compile-time validation: syntax checking, brace balancing, main() detection
- Sandboxed execution: shader complexity limits, uniform count limits, security constraints
- Resource detection: automatic listing of uniforms, attributes, and varying bindings
- Live preview mode: enable/disable real-time shader updates
- Error recovery: automatic revert to last successful shader on compilation failure
- Code formatting: automatic indentation and structure formatting

**Security and safety constraints:**

- File I/O operations are blocked (no imageLoad/imageStore/textureLoad outside safe contexts)
- Forbidden functions (barrier, memoryBarrier, groupMemoryBarrier) are rejected
- Maximum shader complexity score enforced (prevents overly complex computations)
- Maximum uniform count enforced (defaults to 32 uniforms)

**Workflow:**

1. Access shader editor from the rendering/presentation settings panel
2. Write or paste GLSL shader source code
3. Enable "Live Preview" to see compilation errors in real-time
4. Successfully compiled shaders are automatically used for rendering
5. Failed compilations show detailed error messages and automatically revert to the previous shader
6. Use "Revert to Last Valid" button to manually restore a previous shader version

**Limitations:**

- Current implementation supports vertex/fragment shaders only (no geometry, tessellation, or compute shaders)
- Shader binary caching is not yet implemented (full recompilation on each load)
- Per-model shader storage/persistence is not yet integrated (shaders are session-only)
- SPIR-V compilation targets are not available in this build (GLSL source only)

**Notes:**

The shader editor is provided as an experimental pathway for advanced customization. Ensure your shaders are thoroughly tested in your target environment before production use.

---

## External Data Import Status

Supported formats:

- **CSV**: Comma-separated numeric grid values; plain text format for tabular data.
- **PGM image** (`P2`/`P5`): Portable Graymap (both ASCII and binary modes).
- **GeoTIFF** (optional): Geospatial tagged image format for raster GIS data; requires GDAL library and build flag `-DWS_ENABLE_GEOTIFF=ON`.
- **NetCDF** (optional): Network Common Data Form for multidimensional scientific data; requires NetCDF C++ library and build flag `-DWS_ENABLE_NETCDF=ON`.

**Import workflow:**

1. Create a new world and select a model.
2. In the "Initialize" tab, choose "Load from file" and select a supported data file.
3. The imported grid will be automatically:
   - Resampled to match the model's grid dimensions (bilinear interpolation).
   - Normalized to the parameter's valid domain range.
4. Click "Create World" to initialize with the imported data.

**Optional dependency installation:**

To enable GeoTIFF and NetCDF support, install the required libraries and rebuild with feature flags:

```bash
# On Ubuntu/Debian:
apt-get install gdal-bin libgdal-dev libnetcdf-c++4-dev

# On macOS:
brew install gdal netcdf-cxx

# On Windows (with vcpkg):
vcpkg install gdal:x64-windows netcdf-cpp:x64-windows
```

Then rebuild:
```bash
cmake .. -DWS_ENABLE_GEOTIFF=ON -DWS_ENABLE_NETCDF=ON
cmake --build . --config Release
```

---

## Troubleshooting

### Simulation Diverges (Values NaN or Inf)

**Cause:** Numerical instability, usually from too-large time step or diffusion coefficient.

**Solution:**
1. Reduce time step (dt) by half
2. Reduce diffusion coefficient
3. Check model equations for correct signs
4. Enable clipping: clamp all state variables to valid domain

### Simulation Runs Too Slow

**Cause:** Large grid (>1024×1024) or complex interactions.

**Solutions:**
1. Reduce grid size (coarser resolution)
2. Enable SIMD optimization: rebuild with `-DCMAKE_BUILD_TYPE=Release`
3. Reduce refresh rate (render every Nth step, not every step)
4. Close other applications to free RAM

### Model Won't Build

**Cause:** Syntax error in model definition.

**Solution:**
1. Check "Build" panel for error messages
2. Red underlines in Model Editor show problematic lines
3. Common mistakes:
   - Undefined variable names
   - Type mismatches (integer vs float)
   - Dimensional inconsistencies (adding temperature to pressure)
   - Missing variable definitions

### Checkpoint Won't Load

**Cause:** Model mismatch or corrupted file.

**Solution:**
1. Ensure the checkpoint was created with the **same model version**
2. Check file is not corrupted: look for `WSCP` magic number at start
3. Try loading a different checkpoint
4. If all else fails, create a new world

---

## FAQ

### Q: Can I run simulations on GPU?

**A:** Simulation logic execution is **CPU-only** to ensure deterministic, bit-reproducible results across platforms. 

The GLSL shader editor supports custom visualization rules for rendering, but shader code does not affect simulation logic. Rendering can utilize GPU hardware, but the core simulation stepping (diffusion, advection, subsystem interactions) always runs on CPU.

GPU compute for simulation logic is a planned future optimization, not the current baseline. If your research requires bit-exact reproducibility, CPU execution is maintained as the deterministic guarantee.

### Q: How do I export results?

**A:** Available export paths depend on the specific panel/workflow. Confirm available options in the active UI context before running long jobs.

For time-series probes, CSV export is supported.

Typical flow:
1. Click "Export" menu
2. Choose a currently available format
3. Select variables to export
4. Specify output folder

### Q: What time integrators are available?

**A:** Built-in runtime registry includes `explicit_euler`, `rk2_midpoint`, `rk3_heun`, `semi_implicit_euler`, `velocity_verlet`, `crank_nicolson`, and `rk4`.

Legacy-friendly aliases are also accepted, including `Euler Explicit`, `RK2`, `RK3`, `Semi-Implicit Euler`, `Verlet`, `Velocity Verlet`, `Crank-Nicolson`, and `RK4`.

Integrator selection can be changed live while a runtime session is active (no restart required). Profile/world saves persist the selected integrator id.

### Q: Can I couple multiple models?

**A:** Currently, one model per simulation. Multi-model coupling is a planned advanced feature.

### Q: How do I verify my model is correct?

**A:** Best practices:
1. Test against analytical solutions if available
2. Check conservation laws (mass, energy) if applicable
3. Verify boundary conditions are applied correctly
4. Compare results with published papers or benchmark data
5. Run determinism tests (same seed → same result)

### Q: How much memory do I need?

**A:** Memory usage is roughly:
- 4 bytes per variable per cell
- 256×256 grid: ~250 KB per variable
- 1024×1024 grid: ~4 MB per variable
- 2048×2048 grid: ~16 MB per variable

10 variables on a 1024×1024 grid ≈ 40 MB.

---

**For more information, see the Developer Guide and API Reference.**
