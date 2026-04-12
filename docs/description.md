# Project Description: World-Simulator

This document is the project-level description for World-Simulator. It defines product intent, architecture, and feature scope in an implementation-agnostic form.

## 1. Introduction and Project Philosophy

### 1.1. Overall Vision
The objective of this project is to design a standalone, highly portable, and interactive software application dedicated to simulating complex physical systems in two dimensions (with potential extension to higher dimensions). The core principle of this application is **absolute decoupling** between simulation logic (the "Model") and the dynamic system state data (the "State" or "World State").

This separation allows users to transport, share, and reproduce complex simulation experiments using only two files: a physical model definition file and a binary state dump file.

### 1.2. Foundational Principles
1.  **Ultimate Generalization (Meta-Simulation):** The application is not limited to weather or wave propagation. It is a "meta-container" capable of executing any grid-based system of partial differential equations (PDE) or ordinary differential equations (ODE), including wave propagation, population dynamics, cellular automata, and fluid mechanics.
2.  **Interactive and Real-Time:** The simulation is not a "black box." Users can intervene at any moment (step X) to modify a parameter, inject a forced perturbation (e.g., adding rain to cell Y), or change the integration algorithm; these modifications must be historically tracked and reproducible.
3.  **Full Portability:** The simulation is "packaged." Generate a model file (the rules) and a state file (checkpoints or complete evolution of selected parameters) to resume exactly the same simulation later or on another machine.

### Target Audience
*   **Researchers and Scientists:** A testing environment for physical, ecological, economic, and other hypotheses.
*   **Educators and Students:** A pedagogical tool for interactively visualizing complex phenomena.
The application must be designed to be accessible to both technical and non-technical users, with an intuitive interface and advanced capabilities for experienced users. Every feature should also support deeper advanced usage; for example, the formula editor should offer visual editing for beginners while also allowing textual editing for advanced users who want to write equation code directly.

## 2. Data Description: The Modeling Language

The system uses a `.simmodel` package as the descriptive model manifest. In current builds, the editable representation is `model.json` and computational logic is stored in `logic.ir`; together they tell the C++ engine what to compute.

### 2.1. Variable Management (Graph Nodes)
Each variable in the model is a strictly typed entity. Variables are classified along two axes:

**A. Support Classification (Space)**
*   **Global Variable (`global`):** A single value for the whole system (e.g., `time`, `sunrise_time`). It represents a unique scalar controlling the simulation.
*   **Cell Variable (`cell`):** A 2D array of values, one per grid cell (e.g., `state.scalar_a`, `state.scalar_b`). This is the core of the mesh.

**B. Role Classification (Function)**
*   **`parameter`:** Configuration values that are typically fixed or rarely changed (e.g., `pressure_scale_height`). They define the model's physical constants.
*   **`state`:** Variables whose values evolve at each time step (the unknowns we solve for, e.g., `state.scalar_a`, `state.scalar_b`).
*   **`derived`:** Variables computed from others at each step, read-only (e.g., `relative_humidity`, `time_of_day`). They are not time-integrated.
*   **`forcing`:** External inputs imposed on the system (e.g., `forcing.input_a`, `forcing.input_b`).
*   **`auxiliary`:** Intermediate variables used in calculations but not exposed to users (e.g., `state_scalar_laplacian`).

### 2.2. Validity Domains (Physical Constraints)
To guarantee physical consistency, each variable references a **Domain**.
*   **Interval Type:** Defines a physical minimum and maximum bound (e.g., water cannot be at -1000°C, relative humidity must stay between 0 and 1).
*   **Categorical Type:** A closed set of allowed values (for cellular automata, for example).
*   *UI Implication:* The interface automatically blocks user input outside allowed bounds (clamping or error). It must still be possible to force a violation for robustness testing by changing the max/min values in advanced settings.
*   **Cross-Validation:** Some domains may depend on other variables (e.g., `relative_humidity` must be between 0 and 1, but also must not exceed `1 - precipitation_flux/max_precipitation_flux`). (Invariant physics checks: the system must detect and report these violations, whether caused by user input or model interactions, to prevent propagation of non-physical states.)

### 2.3. Computation Stages (`stages`)
Executing one time step ($\Delta t$) is not a single monolithic compute block. It is decomposed into strictly ordered **Stages**.
1.  **Temporal Decoupling:** Advance global time (`time`).
2.  **External Forcing:** Compute forcing variables (sun, wind) based on time.
3.  **Physical Processes:** Transport (advection), diffusion, source/sink terms (chemistry/bio).
4.  **Diagnostics:** Compute secondary variables.
5.  **Constraints:** Apply clamping rules (e.g., ensure constrained state variables remain within declared domains).

## 3. Functional Application Architecture

The application is divided into several interconnected workspaces.

### 3.1. Module 1: Model Editor ("Visual Graph Editor")
This is the creation workspace. The user does not have to code directly; they can draw or edit logic visually. Before entering this interface, the user should see a selection screen listing all available models (e.g., Weather, Ecology, Wave Propagation), with options to create a new model from scratch or from a template. The interface must be flexible enough to support both visual editing and text editing (for advanced users who want to write equation code directly). The model list must also support model management operations: duplicate an existing model to create a variant, delete, rename, and related actions.
*   **Interaction Graph View:**
    *   Node-based display where each **Variable** (cell or global) is a node.
    *   **Interactions** (transfer from variable A to B through a physical equation) are represented as connections.
    *   Users should be able to see dependencies at a glance ("If I change `T`, does it affect my shock waves?").
    *   Editing should be interactive within the same window, without requiring a separate text editor, even if some features still require contextual text input, especially for equations.
*   **Model Validation:**
    *   Detect unused variables.
    *   Check dimensional consistency (unit checks: seconds cannot be added to meters).
*   **Numerical Scheme Management:**
    *   Select the global solver (`explicit_euler`, `RK4`, `Verlet`).
    *   Configure spatial schemes (central difference for diffusion, upwind for advection).
*   **Formula Editor:**
    *   Interface for entering mathematical equations (e.g., `state_next = state_current + (diffusion_coefficient * laplacian(state_current)) * dt`).
    *   Built-in function support (e.g., `laplacian()`, `gradient()`, `perlin_noise()`).
    *   Real-time syntax and semantic validation (e.g., "Error: `diffusion_coefficient` is not defined" or "Warning: possible division by zero if `dt` is too large"). Until the formula is validated by both the user and the engine, it must not affect the simulation, but it must still be saved in the model so the user can return to it later.
*   **Model Templates:** Library of prebuilt models (Weather, Ecology, Wave Propagation) for quick starts.
*   **Export/Import:** Save models as `.simmodel` packages and allow sharing/loading models created by other users.
*   **Versioning:** Model change history with rollback support.

### 3.2. Module 2: World Generator ("World Builder")
Before running a simulation, the grid state must be initialized. Before entering this creation interface, users should see a selection screen listing all worlds available for the chosen model (e.g., "Weather - World 1", "Civilization - World 2") and the option to create a new world from scratch or from a template. World management should mirror model management (duplicate, delete, rename, and other operations).
*   **Procedural Generators (Advanced Minecraft Style):**
    *   Support multiple noise layers (Perlin, Simplex, Wavelet).
    *   **Seeding:** Each world has a unique seed. The system guarantees that the same seed produces exactly the same selected values for each cell variable on any machine.
*   **Manual Editing:**
    *   "Paint" brush for local value editing (e.g., carve a hole, add a mountain).
    *   Selection tools (e.g., "Fill a region with a specific value").
*   **Initialization Templates:** Prebuilt worlds for different scenarios or models.
*   **Real Data Import:** Ability to import real geographic data (e.g., DEM for altitude) to create simulations based on real locations.
*   **State Validation:** Verify that initial values respect model constraints (e.g., no water at -10°C).

### 3.3. Module 3: Live Simulation Interface ("Runtime")
This is the core user experience.
*   **Split Multi-View Visualization:**
    *   Ability to open as many independent views as needed.
    *   Each view can display a different variable, or the same variable with different rendering modes and overlays (e.g., heatmap for scalar fields, vectors for transport fields, contours for derived metrics, or multi-variable overlays).
    *   Rendering modes: heatmap (colors), vectors, contour lines. Also support a custom display mode where users define rendering rules based on conditions (e.g., "If `state.scalar_a > threshold`, color red, else blue"). Custom render presets must be saveable and reusable across simulations.
    *   An advanced version of custom rendering may allow user-defined GLSL code (shader-like syntax or embedded scripting) for rendering rules. Complex rule sets must be possible (e.g., "If `terrain_height > 30` and `plant_density > 0.2`, display green cells for plants").
    *   Strong support for heatmap normalization (global vs local normalization, linear vs logarithmic scales, and time-dependent options).
    *   Smart rendering optimization for large grids (dynamic downsampling, GPU rendering, etc.), and optional compute pruning for values that are only displayed and do not need checkpoint persistence.
*   **Temporal Control Console:**
    *   Global time slider.
    *   Buttons: Play, Pause, Step Forward ($\Delta t$), Step Backward (optional / snapshot-based).
    *   Display current `time` and current `step`.
    *   Support "Time Jump" (jump to a specific step) and return to saved checkpoints.
    *   In paused mode, support "Time Scrubbing" (move backward/forward in time to inspect changes).
    *   Allow changing $\Delta t$ live to observe temporal-resolution effects, while still saving variable state changes.
*   **Real-Time Injection ("Live Patching"):**
    *   Example: user manually modifies a scalar field at step 500; the system records this as an event at `t=500`.
    *   Enables reproducible test scenarios (e.g., "What happens if a wall appears at cell X,Y at `t=100`?").
*   **Simulation Parameter Control Window:**
    Floating window or panel attached to the simulation.
    *   **Simulation Parameters:** Modify global parameters live (e.g., `solar_peak_irradiance`, `diffusion_coefficient`).
    *   **Numerical Scheme:** Change solver or spatial schemes during simulation to compare effects (e.g., switch from `explicit_euler` to `RK4`).
    *   **Perturbation Tools:** Inject forcing terms (e.g., "Add a rain storm over region X,Y for the next 10 steps").
    *   **Checkpoint Management:** Manually create checkpoints at any step to save current state and return later.
    *   **Modification Log:** Display all manual user changes (e.g., "t=500: `state.scalar_a` changed to a new value") and allow clicking an entry to return to that point in time.
    *   **Paint Tool:** Paint directly on the grid to modify cell-variable values (e.g., draw a mountain, carve a valley, add vegetation, apply an event).
    *   **Selection Tool:** Select a region to apply bulk modifications (e.g., "Fill this area with a specific value").
    *   **Forcing Tool:** Apply forcing to a specific region or variable (e.g., "Add a heat source in this area for the next 5 steps").
*   **Analysis and Diagnostics Window:**
    Floating window or panel attached to the simulation.
    *   **Time-Series Graphs:** Select a cell or global mean and view variable evolution over time.
    *   **Histograms:** Spatial distribution of values (e.g., "How many cells have altitude > 500m?").
    *   **Validation:** Display constraint violations (e.g., "Warning: state variable exceeds max defined domain at cell (54,12)").
In general, all display settings must be editable live, without restarting the simulation, and with saved changes. These preferences must be simulation-specific (different display presets per simulation) and restored automatically when reloading a simulation.

## 4. Technical File Specifications

### 4.1. The Model File (`.simmodel`)
This file is a structured **multi-representation container** (ZIP archive). This core architectural choice combines modular expressiveness for users with extreme runtime performance, while supporting live patching.

The `.simmodel` format has an internal organization that isolates each concern:
*   **`metadata.json` and `version.json`:** Model name, version, physical context, engine compatibility versioning, and identity hash (`runSignature`).
*   **`model.bin` (Data):** Structured binary format (e.g., FlatBuffers) as the source of truth for nodes, variables (global/cell), and grid definition. The graphical editor manipulates a UI-only `model.json` counterpart.
*   **`logic.ir` (Mathematical Logic):** World-Simulator's core value. Instead of interpreting a slow AST or high-level text manifest at runtime, logic is stored in a strictly typed **Intermediate Representation (IR)** (DAG of mathematical operations). This IR goes through an optimization pipeline.
*   **Compiled Artifacts (`logic.opt.bin`, `logic.cpu.bin`, `logic.gpu.spv`):** The IR is lowered dynamically to high-performance targets such as SIMD-vectorized native machine code through LLVM/asmjit (CPU) or precompiled SPIR-V shaders (GPU).
*   **`layout.bin` (Memory):** Dynamically manages the SoA (Structure of Arrays) layout to guarantee contiguous memory access regardless of user-added variables.

This strict separation is fully documented in the engine technical specification file: **`model_format_doc.md`**.

### 4.2. The State / World File
This file contains simulation state and associated memory.
*   **Header:** Reference to the compatible model file.
*   **Data Grid:** Raw memory buffer for variables.
*   **Generation Seed:** For procedurally initialized variables. With deterministic RNG, the same seed generates exactly the same values for every cell variable on any machine.
*   **Checkpoints:** Full saves at intervals (e.g., t=0, t=1000, t=2000).
    Checkpoint frequency must be configurable (e.g., every 100 steps), with manual checkpoint creation at any step. It should also be possible to save some variables at different frequencies (e.g., save `state.scalar_a` every 50 steps, `state.scalar_b` every 200 steps, and others not at all). In the initial version, users can only return to saved steps, not intermediate ones (interpolation may be added later).
*   **Modification Journal (Action Log):** List of manual user changes from previous sessions for replay and replication (e.g., "t=500: `state.scalar_a` changed from `v1` to `v2`").

## 5. Detailed Key Features

### 5.1. Mesh System (Spatial Context)
*   **Regularity:** The mesh is structured (regular X, Y grid). A future extension to unstructured meshes is possible, but the project starts with a regular grid for simplicity.
*   **Resolution:** Configurable when creating the world (e.g., 100x100, 1000x1000). Higher resolution increases C++ CPU demand.
*   **Neighborhood:** The system computes neighbor indices instantly (North, South, East, West) for Laplacian and gradient operators. It must also support more complex neighborhoods (e.g., 8-neighbor Moore neighborhood, or custom neighborhoods for specific interactions).
*   **Boundaries:** Support multiple boundary conditions globally or per-parameter (e.g., periodic, fixed, reflecting, Dirichlet, etc.).
*   **Dimensional Extension:** Although the project starts in 2D, architecture must support future extension to additional dimensions. This affects both data storage and spatial operator definitions. Existing 2D simulations must remain unaffected and continue to run in 2D, while new simulations may be created in 3D or higher dimensions.

### 5.2. Temporal System (Time Integrators)
Solver choice directly affects stability.
*   **Explicit Euler:** First-order method; very simple and fast, but strongly stability-limited (small time steps required for stiff systems).
*   **Implicit Euler:** First-order method; unconditionally stable for some problems, suitable for stiff systems but requires solving a system at each step.
*   **Semi-Implicit Euler:** Symplectic variant, more stable than explicit Euler and better at conserving energy in dynamic systems.
*   **Verlet:** Second-order symplectic method with excellent energy conservation; widely used in mechanics and molecular dynamics.
*   **Runge-Kutta 2 (RK2):** Second-order method; improves Euler accuracy with moderate cost.
*   **Runge-Kutta 3 (RK3):** Third-order method; good accuracy/stability compromise, often used in fluid simulation.
*   **Runge-Kutta 4 (RK4):** Fourth-order method; robust and accurate standard for many non-stiff problems.
*   **Implicit Runge-Kutta:** Family of stable methods for stiff systems, often higher order but expensive.
*   **Adams-Bashforth:** Explicit multistep method; cost-efficient but stability-sensitive.
*   **Adams-Moulton:** Implicit multistep method; more stable, often used as a corrector.
*   **Predictor-Corrector:** Combines explicit prediction and implicit correction to improve stability and accuracy.
*   **BDF (Backward Differentiation Formula):** Implicit multistep methods; very stable and ideal for strongly stiff systems.
*   **Leapfrog:** Second-order explicit symplectic method with good energy conservation for oscillatory systems.
*   **Velocity Verlet:** Verlet variant with explicit velocity handling, practical for physical simulations.
*   **Crank-Nicolson:** Second-order implicit method, stable and accurate, often used for diffusion equations.
*   **Exponential Methods:** Use operator exponentials; effective for linear or weakly nonlinear stiff systems.
*   **Strang Splitting:** Second-order splitting method; treats different dynamics separately to improve stability and accuracy.
*   **Lie Splitting:** First-order splitting method; simpler but less accurate than Strang.
*   **Symplectic Methods:** Preserve Hamiltonian geometric structure; essential for long-horizon simulations.
*   **Adaptive Methods:** Dynamically adjust time step using local error estimates, providing automatic precision/stability/cost trade-offs.
*   *UI integration in the simulation parameter control module:* Drop-down list for live switching. The system must recompile/reconfigure the pipeline live, without restarting the simulation.

### 5.3. Interactions and Operators
Users define interactions as functional blocks:
*   **Advection:** Transport variable `A` through vector field `B`.
*   **Diffusion:** Balance variable `A` across neighbors with coefficient `C`.
*   **Source/Sink:** Create or remove matter (e.g., evaporation).
*   **Algebraic Computation:** Simple arithmetic composition.
*   **Custom Functions:** Users can define complex functions (e.g., `derived.metric = f(state.scalar_a, state.scalar_b)`).
*   **Logical Conditions:** Conditional interactions (e.g., "If `state.scalar_a > threshold`, then `derived.metric = 0`").
*   **Multiple Interactions:** A variable may be affected by multiple interactions (e.g., a state scalar influenced by diffusion, forcing, and transport).
*   **Execution Order:** Users can reorder stages to observe impact (e.g., apply solar forcing before or after diffusion).
*   **Nonlinear Interactions:** Support nonlinear functions (e.g., `precipitation_flux = k * (relative_humidity^2)`).
*   **Stochastic Interactions:** Support random terms (e.g., "Add Perlin-noise perturbation to a state scalar to simulate turbulence").

## 6. Style and User Experience
The interface must be intuitive, even for non-technical users. The goal is to enable someone with a basic understanding of physical phenomena to create and explore complex simulations without needing to write much code. The interface should be visually appealing, with clear graphics and accessible controls. Advanced users can dive deeper into details, but the base experience must remain smooth and engaging.

### 6.1. Accessibility and Learning
*   **Guides and Tutorials:** Integrate interactive tutorials to guide new users through core features.
*   **Built-in Documentation:** Easy access to documentation for each feature, with usage examples.

### 6.2. Personalization
*   **Visual Themes:** Interface appearance customization options (e.g., dark mode, custom variable colors).
*   **Keyboard Shortcuts:** Shortcut support for common actions (e.g., Play/Pause, Step Forward/Backward).
*   **Flexible Layout:** Allow users to rearrange panels and views according to preference.
*   **Interface State Saving:** Allow users to save UI layout/configuration for future sessions.
*   **Notifications and Alerts:** Notification system for events (e.g., "Simulation complete", "Constraint violation detected").

### 6.3. Performance and Responsiveness
*   **Optimization:** Use optimization techniques to keep the experience smooth even for complex simulations (e.g., multithreading, GPU).
*   **Visual Feedback:** Show performance indicators (e.g., compute time per step) and progress indicators (e.g., world-generation loading bar).
*   **Error Handling:** Clear, informative error messages with suggested fixes.
The interface must therefore be heavily optimized, because live patching must support on-the-fly changes without freezing or lag, even for complex simulations. The rendering system must also be optimized so that high-resolution / large-grid visualizations remain fluid.

## 7. Example User Workflow

1.  **Creation:** The user opens the default `.simmodel` package and sees the "Weather" model.
2.  **Modification:** They decide to add a "locust population" variable and add it in `variables` with `role: state`.
3.  **Linking:** They create a link in `stages`: locust population consumes plants (from `leaf_area_index`).
4.  **Generation:** They open the generator, choose a seed, and generate a map.
5.  **Simulation:** They run the simulation. The map starts empty. They paint vegetation.
6.  **Observation:** They watch a selected scalar field evolve as a heatmap.
7.  **Experiment:** They pause at step 500, reduce `solar_peak_irradiance` by half, add note "Reduced sunlight," then resume.
8.  **Analysis:** They observe the selected scalar response dropping, locust population decreasing, and vegetation regrowing.
9.  **Save:** They save both model and state. They can share both files with a colleague who can load and reproduce the exact same simulation.
10. **Exploration:** They can use time scrubbing to inspect intermediate steps, or time jump to key points (e.g., `t=1000`) to inspect long-term effects.
