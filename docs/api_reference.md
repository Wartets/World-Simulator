# World-Simulator API Reference

The public headers under `include/ws/` are authoritative. This document is a navigational reference that highlights the current project-facing API areas and representative entry points.

## Core Classes and APIs

### Runtime (`include/ws/core/runtime.hpp`)

Main orchestration class for simulation execution and state management.

```cpp
class Runtime {
public:
    // Initialization
    Runtime(const Profile& profile, uint32_t gridWidth, uint32_t gridHeight);
    void initialize();
    
    // Stepping
    void step();
    uint64_t getCurrentStep() const;
    float getCurrentTime() const;
    
    // Input
    void applyInputPatch(const InputPatch& patch);
    void enqueueInteraction(const InteractionEvent& event);
    
    // Checkpoint/Restore
    Checkpoint captureCheckpoint() const;
    void loadCheckpoint(const Checkpoint& cp);
    bool isCheckpointCompatible(const Checkpoint& cp) const;
    
    // State access
    StateStore& getStateStore();
    const Scheduler& getScheduler() const;
    
    // Observability
    uint64_t getStateHash() const;
    RunSignature getRunSignature() const;
    EventLog getEventLog() const;
};
```

**Usage Example:**

```cpp
#include "ws/core/runtime.hpp"

int main() {
    Profile profile = Profile::defaultProfile();
    Runtime runtime(profile, 256, 256);
    runtime.initialize();
    
    // Simulate 1000 steps
    for (int i = 0; i < 1000; ++i) {
        runtime.step();
    }
    
    // Capture state for verification
    Checkpoint cp = runtime.captureCheckpoint();
    std::cout << "Final state hash: " << cp.stateHash << std::endl;
    
    return 0;
}
```

---

### StateStore (`include/ws/core/state_store.hpp`)

Central container for grid data with Structure-of-Arrays layout.

```cpp
class StateStore {
public:
    // Field registration
    FieldHandle registerField(const std::string& name, int sizeBytes);
    FieldHandle requireField(const std::string& name) const;
    
    // Scalar access (fast)
    float trySampleScalarFast(FieldHandle h, int x, int y);
    void setScalarFast(FieldHandle h, int x, int y, float value);
    
    // Scalar access (safe, with bounds checking)
    float sampleScalar(FieldHandle h, int x, int y, 
                      BoundaryCondition::Type bc);
    
    // Aggregate access
    std::vector<float> getAllValues(FieldHandle h);
    void setAllValues(FieldHandle h, const std::vector<float>& values);
    
    // Dimensionality
    int getWidth() const;
    int getHeight() const;
    size_t getTotalCells() const;
    
    // Hashing (for determinism checks)
    uint64_t computeHash() const;
};
```

**Usage Example:**

```cpp
StateStore state(256, 256);
auto tempHandle = state.registerField("temperature", sizeof(float));

// Write all cells to 288K
std::vector<float> temps(256*256, 288.0f);
state.setAllValues(tempHandle, temps);

// Read a specific cell
float val = state.trySampleScalarFast(tempHandle, 128, 128);

// Compute hash for determinism verification
uint64_t hash = state.computeHash();
```

---

### Scheduler (`include/ws/core/scheduler.hpp`)

DAG-based execution engine for subsystems.

```cpp
class Scheduler {
public:
    // Register a subsystem
    void enqueueSubsystem(const std::string& name, SubsystemFn fn,
                         const std::set<std::string>& dependencies = {});
    
    // Execute all subsystems in dependency order
    void step(StateStore& state, float dt, const RuntimeContext& ctx);
    
    // Introspection
    const std::set<std::string>& getSubsystemNames() const;
    std::vector<std::string> getTopologicalOrder() const;
    
    // Clear all registered subsystems
    void clear();
};

// Subsystem function signature
using SubsystemFn = std::function<void(StateStore&, float dt, 
                                       const RuntimeContext&)>;
```

**Usage Example:**

```cpp
Scheduler scheduler;

// Register subsystems with dependencies
scheduler.enqueueSubsystem("diffusion", diffusionSubsystem, {});
scheduler.enqueueSubsystem("advection", advectionSubsystem, {});
scheduler.enqueueSubsystem("coupling", couplingSubsystem, 
                          {"diffusion", "advection"});

// Execute in order: diffusion and advection (parallel), then coupling
RuntimeContext ctx{256, 256, 0, 0.01f};
scheduler.step(state, 0.01f, ctx);
```

---

### Neighborhood (`include/ws/core/neighborhood.hpp`)

Spatial stencil definitions for generalized neighbor access.

```cpp
enum class NeighborhoodType {
    Moore4,       // 4-connected (cardinal)
    Moore8,       // 8-connected (cardinal + diagonal)
    Moore12,      // Extended Moore (12-neighbor)
    Moore24,      // Extended Moore (24-neighbor)
    Custom        // User-defined offsets
};

class NeighborhoodStencil {
public:
    // Factory methods
    static NeighborhoodStencil createMoore4();
    static NeighborhoodStencil createMoore8();
    static NeighborhoodStencil createMoore12();
    static NeighborhoodStencil createMoore24();
    static NeighborhoodStencil createCustom(const std::vector<std::pair<int,int>>& offsets);
    
    // Apply stencil with boundary handler
    using StencilCallback = void(*)(float value);
    void apply(const StateStore& state, FieldHandle field, int x, int y,
              const BoundaryHandler& handler, StencilCallback cb);
    
    // Get offsets
    const std::vector<std::pair<int,int>>& getOffsets() const;
};

class BoundaryHandler {
public:
    enum class Type {
        Periodic,      // Wrap around
        Dirichlet,     // Fixed value at edge
        Neumann,       // Zero gradient at edge
        Reflecting,    // Mirror at edge
        Absorbing      // Zero value at edge
    };
    
    BoundaryHandler(Type t, float boundaryValue = 0.0f);
    float getValue(const StateStore& state, FieldHandle field,
                  int x, int y, int width, int height) const;
};
```

**Usage Example:**

```cpp
// Create an 8-neighbor Laplacian stencil
auto stencil = NeighborhoodStencil::createMoore8();
BoundaryHandler boundary(BoundaryHandler::Type::Periodic);

float laplacian = 0.0f;
auto cb = [&](float val) { laplacian += val; };
stencil.apply(state, tempHandle, x, y, boundary, cb);
laplacian -= 8.0f * state.trySampleScalarFast(tempHandle, x, y);  // Subtract center
```

---

### DeterministicRNG (`include/ws/core/random.hpp`)

Reproducible random number generation seeded by cell position.

```cpp
class DeterministicRNG {
public:
    // Initialize with global seed
    DeterministicRNG(uint32_t globalSeed);
    
    // Seed by cell position and step (call once per cell)
    void seedCell(int x, int y, uint64_t step);
    
    // Sampling methods (all reproducible given same seed)
    float uniform();                     // [0, 1)
    float uniform(float min, float max); // [min, max)
    int32_t uniformInt(int32_t min, int32_t max);
    float gaussian(float mean = 0.0f, float stddev = 1.0f);
    
    // Deterministic noise (value noise, Perlin-like)
    float valueNoise2D(float x, float y);
    float perlin2D(float x, float y);
};
```

**Usage Example:**

```cpp
DeterministicRNG rng(globalSeed);

for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
        // Seed by cell and current step
        rng.seedCell(x, y, currentStep);
        
        // Draw reproducible samples
        float noise = rng.uniform(-1.0f, 1.0f);
        float gauss = rng.gaussian(0.0f, 0.1f);
    }
}
```

---

### Multi-Dimensional Support (`include/ws/core/multidim_support.hpp`)

Generalized grid indexing for 1D-5D grids.

```cpp
class GridDimensions {
public:
    // Initialize with sizes (up to 5D)
    GridDimensions(int d0);
    GridDimensions(int d0, int d1);
    GridDimensions(int d0, int d1, int d2);
    GridDimensions(int d0, int d1, int d2, int d3);
    GridDimensions(int d0, int d1, int d2, int d3, int d4);
    
    // Boundary conditions per dimension
    void setBoundaryCondition(int dim, BoundaryCondition::Type type);
    BoundaryCondition::Type getBoundaryCondition(int dim) const;
    
    // Queries
    int getRank() const; // Number of dimensions
    int getSize(int dim) const;
    size_t getTotalSize() const;
};

class GridStrides {
public:
    GridStrides(const GridDimensions& dims);
    
    // Convert coordinates to linear index
    int linearIndex(const std::vector<int>& coords) const;
    
    // Convert linear index to coordinates
    std::vector<int> indexToCoords(int index) const;
    
    // Get stride for each dimension
    int getStride(int dim) const;
};

class MultiDimBoundaryResolver {
public:
    MultiDimBoundaryResolver(const GridDimensions& dims);
    
    // Apply boundary conditions per dimension
    int resolveBoundary(int dim, int coord) const;
};
```

**Usage Example:**

```cpp
// Create 3D grid (128×128×64) with periodic X/Y, Dirichlet Z
GridDimensions dims(128, 128, 64);
dims.setBoundaryCondition(0, BoundaryCondition::Type::Periodic);
dims.setBoundaryCondition(1, BoundaryCondition::Type::Periodic);
dims.setBoundaryCondition(2, BoundaryCondition::Type::Dirichlet);

GridStrides strides(dims);

// Access cell (64, 64, 32)
int idx = strides.linearIndex({64, 64, 32});
float value = grid3d[idx];

// Convert back to coordinates
auto coords = strides.indexToCoords(idx);  // {64, 64, 32}
```

---

### Checkpoint I/O (`include/ws/app/checkpoint_io.hpp`)

Serialization and deserialization of simulation snapshots.

```cpp
class CheckpointIO {
public:
    // Save checkpoint to file
    static void saveCheckpoint(const Checkpoint& cp, 
                              const std::string& filename);
    
    // Load checkpoint from file
    static Checkpoint loadCheckpoint(const std::string& filename);
    
    // Verify file integrity
    static bool isValidCheckpoint(const std::string& filename);
    
    // List all available checkpoints in a directory
    static std::vector<std::string> listCheckpoints(
        const std::string& directory);
};

struct Checkpoint {
    uint64_t stepNumber;
    float simulationTime;
    uint64_t stateHash;
    RunSignature runSignature;
    StateStore snapshot;
    EventLog eventLog;
    Metadata metadata;  // Creation time, model version, etc.
};
```

**Usage Example:**

```cpp
// Save current state
Checkpoint cp = runtime.captureCheckpoint();
CheckpointIO::saveCheckpoint(cp, "checkpoint_step_1000.wscp");

// Later: restore from checkpoint
Checkpoint restored = CheckpointIO::loadCheckpoint("checkpoint_step_1000.wscp");
runtime.loadCheckpoint(restored);

// Verify file before loading
if (CheckpointIO::isValidCheckpoint("my_checkpoint.wscp")) {
    Checkpoint cp = CheckpointIO::loadCheckpoint("my_checkpoint.wscp");
}
```

---

### Profile & Configuration (`include/ws/core/profile.hpp`)

Runtime configuration profiles.

```cpp
struct Profile {
    std::string name;
    ModelTier tier;           // A (fast, low accuracy), B, C (slow, high accuracy)
    TemporalPolicy temporal;  // Uniform, Phased, Multirate
    float timeStep;           // dt
    uint32_t randomSeed;
    bool enableDeterminism;   // Enforce bitwise reproducibility
    std::map<std::string, float> parameters;  // Model-specific params
    
    // Predefined profiles
    static Profile defaultProfile();
    static Profile performanceProfile();
    static Profile accuracyProfile();
};

enum class ModelTier { A, B, C };
enum class TemporalPolicy { Uniform, Phased, Multirate };
```

**Usage Example:**

```cpp
// Use a predefined profile
Profile profile = Profile::accuracyProfile();

// Or customize
Profile custom;
custom.name = "MyModel";
custom.tier = ModelTier::B;
custom.timeStep = 0.001f;
custom.randomSeed = 42;
custom.enableDeterminism = true;

Runtime runtime(custom, width, height);
```

---

## Type Definitions

### RunSignature

```cpp
struct RunSignature {
    uint32_t modelHash;       // Hash of model definition
    uint32_t gridHash;        // Hash of grid dimensions
    uint32_t configHash;      // Hash of configuration
    uint64_t identityHash() const;  // Combined hash for reproducibility
};
```

### InteractionEvent

```cpp
struct InteractionEvent {
    std::string name;         // Event identifier
    int x, y;                 // Cell position
    float magnitude;          // Perturbation amount
    uint64_t stepApplied;     // Which step to apply at
    std::map<std::string, float> parameters;
};
```

### EventLog

```cpp
struct EventLog {
    std::vector<InteractionEvent> events;
    void addEvent(const InteractionEvent& e);
    const std::vector<InteractionEvent>& getAll() const;
};
```

---

## Enums

### BoundaryCondition::Type

```cpp
enum class BoundaryCondition::Type : std::uint8_t {
    Periodic = 0,       // Wrapping edges
    Dirichlet = 1,      // Fixed value at edge
    Neumann = 2,        // Zero gradient at edge
    Reflecting = 3,     // Mirror at edge
    Absorbing = 4       // Zero at edge (sink)
};
```

---

## Building with World-Simulator

### CMake Integration

```cmake
# Include World-Simulator
add_subdirectory(World-Simulator)
target_link_libraries(my_app PRIVATE ws_core)

# Include header
#include "ws/core/runtime.hpp"
```

### Interactive shell and GUI launch routing

The command-line executable starts the interactive runtime shell. Typical workflow:

```text
world_sim
help
model select <id|path>
set grid <width> <height>
set seed <u64>
step
checkpoint <label>
```

The GUI executable uses `include/ws/gui/launch_options.hpp` for startup routing. Recognized launch flags include:

- `--model <path>`
- `--edit-model <path>`
- `--world <name>`
- `--import-world <path>`
- `--checkpoint <path>`
- `--open <path>`

These flags route the GUI into the correct editor, world, or checkpoint workflow.

---

## Error Handling

All APIs use exceptions for error conditions:

```cpp
try {
    auto handle = state.requireField("unknown_field");
} catch (const std::runtime_error& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    // Propagate or handle gracefully
}
```

Common exceptions:
- `std::runtime_error` – Logic errors (invalid field names, incompatible checkpoints)
- `std::out_of_range` – Out-of-bounds access
- `std::invalid_argument` – Invalid input parameters

---

**For usage examples, see the Developer Guide. For architecture details, see the model format documentation.**
