# World-Simulator

World-Simulator is a deterministic, grid-based world simulation framework with:

- a core engine (`ws_core`) that owns simulation state and stepping,
- a CLI frontend (`world_sim`),
- a GUI frontend (`world_sim_gui`).

---

## 1. Core Architecture

The simulation loop is centered on `Runtime` (`include/ws/core/runtime.hpp`, `src/core/runtime.cpp`) and `Scheduler` (`include/ws/core/scheduler.hpp`, `src/core/scheduler.cpp`).

### 1.1 Main components

- **Runtime**
	- Owns `RuntimeConfig`, `StateStore`, `Scheduler`, profile/admission metadata, queues, and observability.
	- Applies input patches/events, advances simulation, updates state header/hash, and handles checkpoints.

- **StateStore**
	- Holds canonical scalar fields on a 2D grid.
	- Supports deterministic sampling, write sessions, hashing, and snapshots.

- **Scheduler**
	- Executes registered subsystems according to temporal policy (`UniformA`, `PhasedB`, `MultiRateC`).
	- Enforces one-writer ownership semantics via admission/conflict arbitration.
	- Applies numeric guardrails and computes stability diagnostics.

- **InteractionCoordinator**
	- Builds the admission report (dependency graph, deterministic order, conflict resolution, reproducibility class/tolerance).

- **ProfileResolver**
	- Validates tier assignment for all required subsystems.
	- Produces profile fingerprint used in run identity and checkpoint compatibility.

---

## 2. Canonical State Variables

Canonical fields are allocated in `Runtime::allocateCanonicalFields()`:

| ID | Variable | Meaning | Typical Range |
|---:|---|---|---|
| 0 | `terrain_elevation_h` | Elevation/height proxy | $[0,1]$ |
| 1 | `surface_water_w` | Surface water mass/intensity | $[0,2]$ (subsystem clamp) |
| 2 | `temperature_T` | Temperature-like scalar | $[220,340]$ |
| 3 | `humidity_q` | Humidity proxy | $[0,1]$ |
| 4 | `wind_u` | Wind-like signed scalar | $[-8,8]$ |
| 5 | `climate_index_c` | Composite climate index | $[-4,4]$ |
| 6 | `fertility_phi` | Soil fertility proxy | $[0,1]$ |
| 7 | `vegetation_v` | Vegetation cover/biomass proxy | $[0,1]$ |
| 8 | `resource_stock_r` | Resource stock | $[0,2.5]$ |
| 9 | `event_signal_e` | Event activation signal | $[0,1]$ |
| 10 | `event_water_delta` | Exogenous water forcing | derived from events |
| 11 | `event_temperature_delta` | Exogenous temperature forcing | derived from events |
| 12 | `bootstrap_marker` | Seeding marker/noise field | $[0,1]$ |
| 13 | `seed_probe` | Seeding probe/noise field | $[0,1]$ |

---

## 3. Runtime Step Pipeline (Deterministic Contract)

For each step, the runtime executes the following order:

1. **Input ingestion**: apply queued `RuntimeInputFrame.scalarPatches`.
2. **Event queue apply**: apply queued `RuntimeEvent.scalarPatches` and record chronology.
3. **Scheduler step**: run subsystem updates using selected temporal policy + guardrails.
4. **State commit metadata**: increment header `stepIndex` and `timestampTicks`, recompute `stateHash`, update snapshot/diagnostics.

This order is intentional and part of deterministic behavior.

---

## 4. Initialization and World Seeding Mathematics

World initialization is computed in `Runtime::start()` before the first scheduler step. The implementation deliberately combines deterministic pseudo-random structure with smooth spatial interpolation so that maps are reproducible (same seed = same world) while remaining spatially coherent.

### 4.1 Helper functions

The seed pipeline uses four core mathematical primitives.

1) **Smooth interpolation kernel**

$$
smooth\_step(t)=x^2(3-2x),\quad x=clamp(t,0,1)
$$

This produces a $C^1$-smooth curve between 0 and 1 and avoids sharp interpolation seams.

2) **Hash-based deterministic sample**

$$
h_{01}(s,x,y)=\frac{\text{top24bits}(mix64(hash(s,x,y)))}{2^{24}-1}
$$

Given integer lattice coordinates $(x,y)$ and seed $s$, this returns a deterministic pseudo-random scalar in $[0,1]$.

3) **Bilinear value-noise reconstruction**

$$
n(x,y)=lerp(lerp(n_{00},n_{10},t_x),lerp(n_{01},n_{11},t_x),t_y)
$$

with $t_x=smooth\_step(x-\lfloor x\rfloor)$ and similarly for $t_y$. In words: sample four corner lattice values and smoothly interpolate within the cell.

4) **Fractal Brownian motion (fBm)**

$$
fBm(x,y)=\frac{\sum_{i=0}^{o-1} a_i\,n_i(f_i x,f_i y)}{\sum_{i=0}^{o-1} a_i},\quad
f_i=\lambda^i,\; a_i=g^i
$$

where $o$ is octave count, $\lambda$ is lacunarity, and $g$ is gain. Lower octaves provide macro structure; higher octaves provide fine detail.

### 4.2 Domain warp

Before evaluating terrain/climate fields, coordinates are warped by additional fBm channels:

$$
w_x=fBm(x\,f_d,y\,f_d),\quad w_y=fBm(x\,f_d,y\,f_d)
$$

$$
x' = x + (w_x-0.5)\,s_w, \quad y' = y + (w_y-0.5)\,s_w
$$

Here, $f_d$ is the detail frequency and $s_w$ is `terrainWarpStrength`. This transformation reduces axis-aligned artifacts and produces more natural coastlines and continental boundaries.

### 4.3 Terrain and hydro-climate seeding

The seeded fields are blended from continental noise, ridge/detail noise, macro-zone biases, archipelago masks, latitude effects, and biome modulation. The exact code path includes several intermediate terms; the equations below capture the dominant forms used by the implementation.

Representative elevation/water/climate seeds:

$$
\text{baseRelief}=clamp(0.50\,c + 0.16\,r + 0.18\,m + \text{regional/biome corrections},0,1)
$$

where $c$ is continental component, $r$ is ridge/detail component, and $m$ is macro-shape term.

$$
h = clamp(0.5 + (\text{elevationRaw}-0.5)\,A_h,\,0,1)
$$

where $A_h$ corresponds to `terrainAmplitude`.

$$
w = clamp(\text{waterBasin} + \text{coastalNoise},0,1)
$$

Water combines a sea-level basin term with coastal/biome noise to avoid uniform shorelines.

$$
T_0 = clamp(0.25 + 0.55\,\text{latitudinal}\cdot\text{banding}\cdot(1-0.35\,\text{polarCooling}) + \cdots,0,1)
$$

This creates latitudinal climate bands and altitude cooling effects.

$$
q_0 = clamp(0.20 + 0.45\,\text{humidityFromWater}\cdot w + \cdots,0,1)
$$

Humidity is seeded from water availability plus regional biome terms.

$$
u_0 = clamp(0.25 + 0.45\,\text{noise} + 0.30|T_0-0.5|,0,1)
$$

Initial wind intensity depends on structured noise and thermal contrast magnitude.

$$
c_0 = clamp(0.45\,T_0 + 0.55\,q_0,0,1)
$$

Climate index starts as a weighted humidity-temperature composite.

$$
\phi_0 = clamp(0.25 + 0.55\,q_0 + 0.20(1-|h-\text{seaLevel}|),0,1)
$$

Soil fertility is favored by humidity and moderate elevation relative to sea level.

$$
v_0 = clamp(0.25 + 0.55\,\phi_0 q_0,0,1),\quad
r_0 = clamp(0.15 + 0.55\,v_0 + 0.15\,w + 0.15\,\text{biomeNoise},0,1)
$$

Vegetation and resources are initialized from fertility/humidity/water-biome interactions.

$$
e_0 = clamp(0.15 + 0.85\,\text{noise},0,1)
$$

Event signal is seeded as a spatially varying stochastic potential. Finally, `event_water_delta` and `event_temperature_delta` are set to 0 so exogenous forcing begins inactive.

---

## 5. Subsystems and Interaction Equations

All subsystem updates are per-cell. Where neighborhood coupling appears, the model uses a 4-neighbor (von Neumann) stencil. In equations below, primes denote next-step values.

Unless otherwise noted, each update is followed by an explicit clamp to enforce numerical bounds.

### 5.0 Declared interaction contracts (read/write sets)

| Subsystem | Declared reads | Declared writes |
|---|---|---|
| `generation` | `seed_probe`, `terrain_elevation_h` | `terrain_elevation_h` |
| `hydrology` | `terrain_elevation_h`, `humidity_q`, `climate_index_c`, `event_water_delta`, `surface_water_w`, `wind_u` | `surface_water_w` |
| `temperature` | `climate_index_c`, `wind_u`, `event_temperature_delta`, `temperature_T`, `humidity_q` | `temperature_T` |
| `humidity` | `surface_water_w`, `temperature_T`, `vegetation_v`, `humidity_q`, `climate_index_c` | `humidity_q` |
| `wind` | `temperature_T`, `terrain_elevation_h`, `humidity_q` | `wind_u` |
| `climate` | `temperature_T`, `humidity_q`, `wind_u`, `climate_index_c`, `surface_water_w` | `climate_index_c` |
| `soil` | `surface_water_w`, `temperature_T`, `fertility_phi`, `climate_index_c` | `fertility_phi` |
| `vegetation` | `fertility_phi`, `humidity_q`, `temperature_T`, `resource_stock_r`, `vegetation_v`, `surface_water_w` | `vegetation_v` |
| `resources` | `fertility_phi`, `vegetation_v`, `climate_index_c`, `resource_stock_r`, `surface_water_w` | `resource_stock_r` |
| `events` | `event_signal_e`, `temperature_T`, `humidity_q` | `event_signal_e`, `event_water_delta`, `event_temperature_delta` |

### 5.1 Generation subsystem (`generation`)

Purpose: slow geomorphological smoothing (erosion-like relaxation) after initial seeding.

Tier A is intentionally a no-op (terrain remains seed-driven). Tiers B/C apply relaxation toward neighborhood average.

Tier B/C erosion smoothing:

$$
\bar h_N = \frac{h_{x-1,y}+h_{x+1,y}+h_{x,y-1}+h_{x,y+1}}{4}
$$

$$
h' = clamp(h + \alpha(\bar h_N-h),0,1),\quad
\alpha=0.01\,(B),\,0.02\,(C)
$$

### 5.2 Hydrology subsystem (`hydrology`)

Purpose: update surface water from humidity input, terrain sink/source effects, climate forcing, and queued event forcing.

Base update combines local terms:

$$
w' = w + 0.015q - 0.010h + 0.003c + \Delta w_{event}
$$

Tier B/C add lateral redistribution (diffusive exchange):

$$
w' \leftarrow w' + \beta(\bar w_N - w),\quad \beta=0.08\,(B),\,0.16\,(C)
$$

Tier C adds wind advection proxy:

$$
w' \leftarrow w' + 0.015\,clamp(u,-8,8)
$$

Interpretation: the model is not a full Navier-Stokes solver; it is a conservative-like scalar transport proxy with bounded dynamics.

Final clamp:

$$
w'\in[0,2]
$$

### 5.3 Temperature subsystem (`temperature`)

Purpose: combine climate background, wind cooling, event cooling/heating impulses, diurnal cycle, and elevation lapse effect.

Diurnal phase:

$$
d = \frac{\text{stepIndex}\bmod 24}{24}
$$

Terrain lapse term (higher elevation tends to reduce temperature):

$$
\ell_h = -5h
$$

Base update:

$$
T' = T + 0.05c - 0.03|u| + \Delta T_{event} + 0.2(d-0.5) + 0.01\ell_h
$$

Tier B/C add spatial thermal smoothing:

$$
T' \leftarrow T' + \gamma(\bar T_N-T),\quad \gamma=0.06\,(B),\,0.10\,(C)
$$

Tier C includes extra humidity coupling:

$$
T' \leftarrow T' + 0.08(q-0.5)
$$

Final clamp:

$$
T'\in[220,340]
$$

### 5.4 Humidity subsystem (`humidity`)

Purpose: estimate local humidity from water availability, thermal stress, and biosphere effects.

Temperature stress:

$$
\sigma_T = clamp\left(\frac{T-285.15}{40},-1,1\right)
$$

Base relation:

$$
q' = 0.40 + 0.22w - 0.12\sigma_T + 0.08v
$$

Tier B/C include directional neighborhood carryover:

$$
q' \leftarrow q' + 0.03 q_{x-1,y}
$$

Tier C couples additional climate signal:

$$
q' \leftarrow q' + 0.06c
$$

Clamp:

$$
q'\in[0,1]
$$

### 5.5 Wind subsystem (`wind`)

Purpose: compute a signed wind proxy from horizontal thermal and terrain gradients, with extra meridional terms at higher tiers.

Base (zonal gradient response):

$$
u' = 0.02(T_{x+1,y}-T_{x-1,y}) - 0.08(h_{x+1,y}-h_{x-1,y})
$$

Tier B/C add meridional thermal gradient response:

$$
u' \leftarrow u' + 0.01(T_{x,y+1}-T_{x,y-1})
$$

Tier C adds humidity-gradient contribution:

$$
u' \leftarrow u' + 0.08(q_{x,y+1}-q_{x,y-1})
$$

Clamp:

$$
u'\in[-8,8]
$$

### 5.6 Climate subsystem (`climate`)

Purpose: produce a composite climate index from thermal anomaly, humidity anomaly, and wind stress.

Thermal normalization term:

$$
\tau = clamp\left(\frac{T-285.15}{20},-3,3\right)
$$

Base:

$$
c' = 0.50\tau + 0.80(q-0.5) - 0.12|u|
$$

Tier B/C include neighborhood blending (higher smoothing in C):

$$
c' \leftarrow \lambda c' + (1-\lambda)\bar c_N,
\quad \lambda=0.85\,(B),\,0.70\,(C)
$$

Tier C adds direct water-climate feedback:

$$
c' \leftarrow c' + 0.12(w-0.5)
$$

Clamp:

$$
c'\in[-4,4]
$$

### 5.7 Soil subsystem (`soil`)

Purpose: update fertility from water, temperature suitability, and climate stress.

Thermal suitability proxy:

$$
s_T = 1-\left|\frac{T-290.15}{35}\right|
$$

Base:

$$
\phi' = 0.35 + 0.30w + 0.30\,clamp(s_T,0,1)
$$

Tier B/C include neighborhood transfer:

$$
\phi' \leftarrow \phi' + 0.05\phi_{x,y+1}
$$

Tier C penalizes extreme climate index magnitudes:

$$
\phi' \leftarrow \phi' - 0.04|c|
$$

Clamp:

$$
\phi'\in[0,1]
$$

### 5.8 Vegetation subsystem (`vegetation`)

Purpose: estimate vegetation potential from fertility, humidity, resources, and temperature suitability.

Thermal suitability:

$$
s_T = 1-\left|\frac{T-289.15}{40}\right|
$$

Base growth proxy:

$$
v' = 0.20\phi + 0.20q + 0.10r + 0.25\,clamp(s_T,0,1)
$$

Tier B/C include lateral vegetation propagation:

$$
v' \leftarrow v' + 0.08 v_{x+1,y}
$$

Tier C adds water-driven growth reinforcement:

$$
v' \leftarrow v' + 0.12w
$$

Clamp:

$$
v'\in[0,1]
$$

### 5.9 Resources subsystem (`resources`)

Purpose: update renewable stock from fertility and vegetation, penalized by harsh climate.

Base relation:

$$
r' = 0.25 + 0.35\phi + 0.25v - 0.04|c|
$$

Tier B/C include neighborhood transport:

$$
r' \leftarrow r' + 0.06 r_{x,y-1}
$$

Tier C adds water contribution:

$$
r' \leftarrow r' + 0.10w
$$

Clamp:

$$
r'\in[0,2.5]
$$

### 5.10 Event subsystem (`events`)

Purpose: convert latent event signal and climatic trigger conditions into explicit forcing channels (`event_water_delta`, `event_temperature_delta`).

Tier-dependent constants control persistence and forcing strength:

- retention: $0.35$ (A), $0.55$ (B), $0.70$ (C)
- exogenous water scale: $0.03$ (A), $0.06$ (B), $0.09$ (C)
- cooling scale: $-0.15$ (A), $-0.30$ (B), $-0.45$ (C)

Trigger rules define when hazardous/hydrologic event patterns activate:

- Tier A: if $T>303.15$ and $q<0.25$, trigger $=0.12$
- Tier B: if $T>301.15$ and $q<0.35$, trigger $=0.14+0.15\,clamp(0.35-q,0,1)$
- Tier C: if $T>299.15$ and $q<0.45$, trigger $=0.20+0.20\,clamp(0.45-q,0,1)$

State update:

$$
e' = clamp(e\cdot\text{retention}+\text{trigger},0,1)
$$

$$
\Delta w_{event}=e'\cdot\text{exogenousScale},\quad
\Delta T_{event}=e'\cdot\text{coolingScale}
$$

---

## 6. Temporal Policies and Execution Modes

Temporal policy is selected by `TemporalPolicy`:

- `UniformA`: single pass over ordered subsystems.
- `PhasedB`: two phases: phase 0 for tier-A subsystems, phase 1 for tier-B/C subsystems.
- `MultiRateC`: micro-stepped execution with adaptive sub-iterations and stability escalation.

### 6.1 UniformA

Uniform mode executes one ordered pass of all active subsystems. It is the simplest and most reproducible stepping policy.

For ordered subsystem list $S$:

$$
\forall s\in S:\; x \leftarrow F_s(x)
$$

Parallel batching can be enabled only when all of the following are true:

- execution policy is not `StrictDeterministic`,
- temporal policy is `UniformA`,
- an admission report exists.

When enabled, the scheduler builds dependency-respecting batches from the admission graph and commits batch outputs deterministically.

### 6.2 PhasedB

Let $S_A$ be tier-A subsystems, $S_{BC}$ be tier-B/C subsystems.

This policy enforces a coarse two-stage operator split: baseline subsystems first, then higher-coupling subsystems.

$$
x \leftarrow \Big(\prod_{s\in S_A}F_s\Big)(x),\quad
x \leftarrow \Big(\prod_{s\in S_{BC}}F_s\Big)(x)
$$

### 6.3 MultiRateC

Multi-rate mode targets stiff/highly coupled configurations. Each macro-step is decomposed into micro-steps; each micro-step may execute multiple adaptive sub-iterations.

For $m$ micro-steps (`multiRateMicroStepCount`), each with adaptive sub-iterations $k$:

$$
x_{j+1} = \underbrace{\Big(\prod_{s\in S}F_s\Big)^{k_j}}_{\text{adaptive}}(x_j)
$$

with:

$$
k_j=
\begin{cases}
k_{\max}, & \text{if predictor drift} > \text{stiffnessDriftThreshold}\\
k_{\min}, & \text{otherwise}
\end{cases}
$$

The drift metric compares the current state to a reference snapshot and is used as a stability trigger:

$$
D(x,\tilde x)=\frac{1}{N}\sum_{i=1}^{N}|x_i-\tilde x_i|
$$

Escalation logic:

- if $D >$ soft limit: apply damping toward reference

$$
x \leftarrow x_{ref} + \eta(x-x_{ref}),\quad \eta=\text{dampingFactor}
$$

- if post-damping drift exceeds hard limit:
	- controlled fallback (once): restore reference and execute phased pass,
	- otherwise safe abort (exception).

In practice, this creates a progressive safety ladder: normal update -> damping -> controlled fallback -> abort.

---

## 7. Numeric Guardrails and Stability Diagnostics

Guardrails (`NumericGuardrailPolicy`) apply each step:

1. **Finite check**: fail-fast on non-finite values.
2. **Clamp** (if enabled):

$$
x_i\leftarrow clamp(x_i, x_{min}, x_{max})
$$

3. **Bounded increment** (if enabled):

$$
\Delta_i=x_i-x_i^{prev},\quad
\text{if }|\Delta_i|>\Delta_{max},\; x_i\leftarrow x_i^{prev}+sign(\Delta_i)\Delta_{max}
$$

This rule prevents single-step spikes from destabilizing the full coupled state.

The scheduler also records diagnostics for auditability:

- drift metric and amplification indicator,
- conservation residuals for `surface_water_w` and `resource_stock_r`,
- executed micro-step and adaptive-iteration counts,
- number of damping/fallback applications,
- final escalation action (`none`, `damping`, `controlled_fallback`, `safe_abort`).

---

## 8. Interaction Graph, Contracts, and Admission

Admission is built directly from declared subsystem contracts. The result is a deterministic dependency graph plus conflict and reproducibility metadata.

### 8.1 Data dependency edges

For variable $v$, for each writer $w$ and reader $r$ ($w\neq r$):

$$
w \xrightarrow[v]{\text{data}} r
$$

Interpretation: if subsystem $r$ reads a variable that subsystem $w$ writes, then $w$ must be ordered before $r$ in any deterministic schedule.

### 8.2 Temporal dependency edges

- `PhasedB`: A-tier nodes get temporal edges to B/C nodes.
- `MultiRateC`: active C-tier strongly coupled subsystems get pairwise temporal-rate edges.

These edges are not variable-specific data edges; they encode policy-level sequencing constraints.

### 8.3 Conflict resolution

If multiple writers exist for same variable, mode is deterministic priority:

1. higher tier wins ($C>B>A$),
2. lexical tie-break.

The selected writer is recorded in the admission report, and non-selected writers are effectively removed from the variable ownership set for execution.

### 8.4 Reproducibility class

Based on number of C-tier subsystems:

- none: `Strict` (tolerance $0.0$)
- $1..3$: `BoundedDivergence` (tolerance $10^{-4}$)
- $>3$: `Exploratory` (tolerance $5\times10^{-3}$)

This classification does not change equations directly; it communicates expected replay sensitivity and tolerance expectations.

### 8.5 Contract validation

Observed read/write access during execution must remain within declared sets. If a subsystem reads or writes an undeclared variable, execution fails fast with a contract violation exception.

---

## 9. Full Parameter Reference

## 9.1 Launch/runtime model parameters (`RuntimeConfig` / `LaunchConfig`)

| Parameter | Type | Default | Description |
|---|---|---|---|
| `seed` | `uint64` | `42` (`LaunchConfig`), `1` (`RuntimeConfig`) | Deterministic global seed. |
| `grid.width` | `uint32` | `128` (`LaunchConfig`) / `16` (`RuntimeConfig`) | Grid width in cells. |
| `grid.height` | `uint32` | `128` (`LaunchConfig`) / `16` (`RuntimeConfig`) | Grid height in cells. |
| `tier` | `ModelTier` | `A` | Convenience tier preset for all required subsystems in shell helpers. |
| `temporalPolicy` | `TemporalPolicy` | `UniformA` | Temporal stepping policy (`uniform`, `phased`, `multirate`). |
| `boundaryMode` | `BoundaryMode` | `Clamp` | Boundary sampling mode in state store. |
| `topologyBackend` | `GridTopologyBackend` | `Cartesian2D` | Topology backend (currently Cartesian 2D). |
| `memoryLayoutPolicy.alignmentBytes` | `uint32` | `64` | Alignment for field storage. |
| `memoryLayoutPolicy.tileWidth` | `uint32` | `64` | Tile width for layout policy. |
| `memoryLayoutPolicy.tileHeight` | `uint32` | `1` | Tile height for layout policy. |
| `unitRegime` | `UnitRegime` | `Normalized` | Unit interpretation mode. |
| `executionPolicyMode` | `ExecutionPolicyMode` | `StrictDeterministic` | Scheduler execution policy. |
| `profileInput.requestedSubsystemTiers` | map | required | Explicit tier for each required subsystem. |
| `profileInput.compatibilityAssumptions` | set | required non-empty | Explicit assumptions required by resolver. |

Required subsystem tier keys:

- `generation`
- `hydrology`
- `temperature`
- `humidity`
- `wind`
- `climate`
- `soil`
- `resources`
- `vegetation`
- `events`
- `temporal`

## 9.2 World generation parameters (`WorldGenerationParams`)

| Parameter | Type | Default | Effect |
|---|---:|---:|---|
| `terrainBaseFrequency` | float | `2.2` | Macro continental frequency. |
| `terrainDetailFrequency` | float | `7.5` | Detail frequency for terrain/noise. |
| `terrainWarpStrength` | float | `0.55` | Domain warp amplitude. |
| `terrainAmplitude` | float | `1.0` | Elevation contrast scaling around 0.5. |
| `terrainRidgeMix` | float | `0.28` | Ridge contribution weight. |
| `terrainOctaves` | int | `5` | fBm octave count. |
| `terrainLacunarity` | float | `2.0` | fBm frequency multiplier per octave. |
| `terrainGain` | float | `0.5` | fBm amplitude multiplier per octave. |
| `seaLevel` | float | `0.48` | Baseline sea level thresholding. |
| `polarCooling` | float | `0.62` | Latitudinal cooling intensity. |
| `latitudeBanding` | float | `1.0` | Climate latitudinal banding strength. |
| `humidityFromWater` | float | `0.52` | Water-to-humidity transfer strength at seed stage. |
| `biomeNoiseStrength` | float | `0.20` | Noise contribution to biome/climate heterogeneity. |
| `islandDensity` | float | `0.58` | Probability/density of archipelago seeds. |
| `islandFalloff` | float | `1.35` | Radial island decay sharpness. |
| `coastlineSharpness` | float | `1.10` | Coast transition sharpness. |
| `archipelagoJitter` | float | `0.85` | Island-center jitter magnitude. |
| `erosionStrength` | float | `0.32` | Terrain erosion modulation strength. |
| `shelfDepth` | float | `0.20` | Continental shelf depth shaping. |

## 9.3 Numeric guardrail parameters (`NumericGuardrailPolicy`)

| Parameter | Type | Default | Description |
|---|---:|---:|---|
| `clampMin` | float | `-1000000.0` | Global lower clamp. |
| `clampMax` | float | `1000000.0` | Global upper clamp. |
| `maxAbsDeltaPerStep` | float | `1000.0` | Per-cell bounded increment cap. |
| `clampEnabled` | bool | `true` | Enable clamp pass. |
| `boundedIncrementEnabled` | bool | `true` | Enable bounded increment pass. |
| `multiRateMicroStepCount` | uint32 | `4` | Number of micro-steps in MultiRateC. |
| `minAdaptiveSubIterations` | uint32 | `1` | Lower adaptive sub-iteration count. |
| `maxAdaptiveSubIterations` | uint32 | `4` | Upper adaptive sub-iteration count. |
| `stiffnessDriftThreshold` | float | `0.10` | Drift threshold for choosing max sub-iterations. |
| `divergenceSoftLimit` | float | `0.30` | Soft divergence threshold (triggers damping). |
| `divergenceHardLimit` | float | `0.75` | Hard divergence threshold (fallback/abort). |
| `dampingFactor` | float | `0.35` | Damping blend toward reference in escalation. |
| `enableControlledFallback` | bool | `true` | Allow one controlled fallback before abort. |

---

## 10. Checkpoints, Identity, and Determinism

- Runtime run identity (`RunSignature.identityHash`) is built from seed, profile digest, temporal policy, grid, boundary mode, unit regime, interaction graph fingerprint, and subsystem set hash.
- Checkpoint load requires:
	- matching run identity hash,
	- matching profile fingerprint.
- Snapshot metadata includes:
	- `stateHash`,
	- `stateHeader` (`stepIndex`, `timestampTicks`, `status`),
	- `payloadBytes`,
	- `reproducibilityClass`,
	- `stabilityDiagnostics`.

---

## 11. Model Tiers and Practical Interpretation

- **Tier A**: baseline local rules, minimal coupling, strictest reproducibility.
- **Tier B**: intermediate coupling, neighborhood blending/exchange.
- **Tier C**: highest coupling, multi-rate temporal policy requirement, stronger cross-variable interactions and stability controls.

Temporal constraints enforced at admission:

- temporal tier A requires `UniformA`,
- temporal tier B requires `PhasedB`,
- temporal tier C requires `MultiRateC`,
- any C-tier subsystem requires temporal tier C and `MultiRateC`.

---