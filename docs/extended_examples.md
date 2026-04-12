# Extended Example Models

This document describes two comprehensive example models demonstrating World-Simulator's capabilities for scientific simulation.

---

## 1. SEIR Epidemiology Model

### Overview

The **SEIR model** is a classic compartmental model in mathematical epidemiology, describing disease spread through a population.

**Compartments:**
- **S (Susceptible):** Healthy individuals who can contract the disease
- **E (Exposed):** Infected but not yet infectious (incubation period)
- **I (Infectious):** Actively infectious, can spread disease to others
- **R (Recovered):** Immune; cannot be reinfected (or temporarily resistant)

### Scientific Background

The SEIR model governs disease dynamics via:

$$\frac{dS}{dt} = -\beta \cdot S \cdot I / N$$

$$\frac{dE}{dt} = \beta \cdot S \cdot I / N - \sigma \cdot E$$

$$\frac{dI}{dt} = \sigma \cdot E - \gamma \cdot I$$

$$\frac{dR}{dt} = \gamma \cdot I$$

Where:
- $\beta$ = transmission rate (contacts × infectivity)
- $\sigma$ = incubation rate (1/incubation period)
- $\gamma$ = recovery rate (1/infectious period)
- $N$ = total population

**Key metric:** Basic reproduction number $R_0 = \beta / \gamma$ (expected secondary infections from one case).

### Spatial Implementation

This model extends classic SEIR to a **spatial grid**, allowing disease to spread locally:

**Modified equations (per cell):**

$$\frac{dS}{dt} = -\beta \cdot S \cdot I / N - \text{outflow to neighbors} + \text{inflow from neighbors}$$

$$\frac{dE}{dt} = \beta \cdot S \cdot I / N - \sigma \cdot E + \text{diffusion}$$

$$\text{(same for I and R)}$$

Where diffusion models movement of individuals between adjacent cells:
$$\text{diffusion} = D \cdot \nabla^2 X$$

This creates **wave-like spread** of infection across space, more realistic than global-mixing compartmental models.

### Model Parameters

| Parameter | Value | Unit | Description |
|-----------|-------|------|-------------|
| $\beta$ | 0.5 | day$^{-1}$ | Transmission rate |
| $\sigma$ | 0.2 | day$^{-1}$ | Incubation rate (5-day mean) |
| $\gamma$ | 0.1 | day$^{-1}$ | Recovery rate (10-day mean) |
| $D$ | 0.01 | cell$^2$/day | Diffusion (movement) |
| $N$ | 1000 | people/cell | Population density |

### Grid Setup & Initial Conditions

**Grid dimensions:** 128×128 cells (covering ~1000 km² with 100 km/cell resolution)

**Initial conditions:**
- All cells: S = 990, E = 0, I = 10, R = 0 (seeding infection at center)
- Center cell (64, 64): S = 950, E = 20, I = 30, R = 0 (outbreak hotspot)

**Boundary conditions:** Periodic (disease can "wrap" geographically, modeling re-entry)

### Expected Behavior

1. **Days 0-10:** Exponential growth in central region
2. **Days 10-30:** Wave front propagates outward at ~10 cells/day
3. **Days 30-50:** Peak infection plateau
4. **Days 50-100:** Gradual decline as susceptible population depletes

**Bifurcation scenarios:**
- If $\beta > \gamma$: Sustained epidemic (endemic)
- If $\beta < \gamma$: Epidemic dies out (controlled)
- If $D$ → 0: Isolated patches (no spread)
- If $D$ → ∞: Homogeneous mixing (global SEIR behavior)

### Simulation workflow

This example is illustrative. In the shipped project, run it through the GUI or the interactive runtime shell:

1. Open `world_sim_gui` or start `world_sim`.
2. Select the example model package.
3. Set the grid to `128 128` and choose a deterministic seed such as `42`.
4. Run the desired number of steps, then capture a checkpoint once the run completes.
5. Use the GUI export tools or checkpoint storage helpers to save snapshots for analysis.

### Visualization & Analysis

Key observables to track:

1. **Total infected over time:**
   ```
   I_total(t) = sum over all cells of I(t)
   Expected: bell-curve peaking around day 30-40
   ```

2. **Spatial spread (wavefront):**
   - Plot I field as heatmap
   - Observe outward propagation from center
   - Measure wavefront velocity: dR/dt ≈ √(2Dγ) ≈ 0.15 cells/hour

3. **Phase portrait (S vs I):**
   - Clockwise spiral in (S, I) plane
   - Trajectory determined by R_0

### Extensions & Variants

**Add vital dynamics:**
```
dS/dt = ... + μN - μS (births, deaths)
```

**Add quarantine:**
```
dI/dt = ... - q·I (isolation reduces transmission)
```

**Add vaccination:**
```
dS/dt = ... - v·S (vaccination removes from susceptible)
```

**Multiple strains:**
```
I = I₁ + I₂ (competition between variants)
```

---

## 2. Ecosystem Predator-Prey Model

### Overview

The **Lotka-Volterra model** (or predator-prey model) describes cyclic population dynamics in a two-species system.

**Species:**
- **Prey (P):** Herbivores with unlimited food, growth limited only by predation
- **Predators (X):** Carnivores dependent on prey for survival

### Scientific Background

Classic Lotka-Volterra equations:

$$\frac{dP}{dt} = \alpha \cdot P - \beta \cdot P \cdot X$$

$$\frac{dX}{dt} = \delta \cdot \beta \cdot P \cdot X - \gamma \cdot X$$

Where:
- $\alpha$ = prey birth rate (exponential growth)
- $\beta$ = predation rate (capture efficiency)
- $\delta$ = energy conversion efficiency (predator gain per prey eaten)
- $\gamma$ = predator death rate (no food → starvation)

**Key property:** Oscillatory cycles with phase lag:
- High prey → predators increase → prey crash → predators starve → cycle repeats

### Spatial Implementation

Adding spatial structure (movement + diffusion):

$$\frac{dP}{dt} = \alpha \cdot P - \beta \cdot P \cdot X + D_P \nabla^2 P$$

$$\frac{dX}{dt} = \delta \cdot \beta \cdot P \cdot X - \gamma \cdot X + D_X \nabla^2 X$$

**Key insight:** With diffusion, traveling waves emerge instead of synchronized global oscillations. Predators chase prey, creating spiral-like spatial patterns.

### Model Parameters

| Parameter | Value | Unit | Description |
|-----------|-------|------|-------------|
| $\alpha$ | 0.1 | day$^{-1}$ | Prey growth rate |
| $\beta$ | 0.002 | (prey·day)$^{-1}$ | Predation efficiency |
| $\delta$ | 0.5 | – | Energy conversion |
| $\gamma$ | 0.1 | day$^{-1}$ | Predator death rate |
| $D_P$ | 0.05 | cell$^2$/day | Prey diffusion (movement) |
| $D_X$ | 0.02 | cell$^2$/day | Predator diffusion (slower) |

### Grid Setup & Initial Conditions

**Grid dimensions:** 256×256 cells (modeling a savanna ecosystem)

**Initial conditions (uniform + noise):**
- Prey density: 100 + random $\in \[−10, 10\]$ per cell
- Predator density: 10 + random $\in \[−1, 1\]$ per cell

**Boundary conditions:** Reflecting (species don't escape boundaries)

### Expected Behavior

**Phase 1 (Days 0-50):** Establishment
- Prey increase rapidly
- Predators lag behind (response delay)
- Local variations drive spatial structure

**Phase 2 (Days 50-200):** Spiral wave formation
- Traveling waves emerge (predators chase prey)
- Spiral rotates outward from generation points
- Characteristic wavelength: λ ≈ 2π√(D_P D_X / (βδ)) ≈ 20-30 cells

**Phase 3 (Days 200+):** Pattern stability
- Multiple spiral arms compete
- Defects and annihilation events (spirals merge)
- Large-scale vortex structure

### Simulation workflow

This example is also illustrative. A current-session workflow looks like this:

1. Select the predator-prey example model in the GUI or runtime shell.
2. Set the grid to `256 256` and seed the run deterministically.
3. Run for the desired step count, then save a named checkpoint.
4. Export frames or time-series data from the GUI panels as needed.

### Visualization & Analysis

Key observables:

1. **Population timeseries (global):**
   ```
   P_total(t) = sum of P over all cells
   X_total(t) = sum of X over all cells
   Expected: oscillations with pred lagging prey
   ```

2. **Spatial patterns:**
   - Heatmap of prey (cool = low, hot = high)
   - Overlay predator field as contours
   - Observe spiral wave structure

3. **Phase portrait (P vs X):**
   - Closed loop (limit cycle) per region
   - Center of oscillation: P* ≈ γ/(δβ), X* ≈ α/β

4. **Wave velocity:**
   - Measure spiral arm propagation: v ≈ √(2αD_P) ≈ 0.1 cells/hour

### Bifurcation & Stability

**Equilibrium:** (P*, X*) = (γ/(δβ), α/β) is a center (marginally stable)

**Stability region:**
- If $\alpha$ too low: predators outcompete prey → extinction
- If $\gamma$ too high: predators starve → prey explode → unrealistic
- If $D_P$ >> $D_X$: prey escape easily → predators starve
- If $D_P$ << $D_X$: predators catch prey easily → prey crash

**Typical dynamics:**
- Non-spatial (D=0): perfectly periodic oscillations
- Weakly spatial (D small): slightly damped, local oscillations
- Strongly spatial (D large): traveling waves, spiral patterns, chaos at extreme D

### Extensions & Variants

**Add prey competition:**
```
dP/dt = ... - c·P² (intraspecific competition)
```

**Add starvation threshold:**
```
if P < P_min: dX/dt = -γ·X - X (accelerated death when starving)
```

**Add refuge zones:**
```
dP/dt = ... at safe areas only (spatial heterogeneity)
```

**Three-species system:**
```
Add plant (vegetation) consumed by prey, regenerating slowly
Creates food-web dynamics
```

**Evolution of traits:**
```
Track predator attack efficiency β as evolving trait
Coevolutionary arms race: fast prey select for faster predators
```

---

## 3. Implementation Notes

### Numerical Stability

Both models use **semi-implicit Euler** with operator splitting:

1. **Growth/decay step:** Explicit Euler
   ```cpp
   S(t+dt) = S(t) * exp((-beta * I / N) * dt)
   I(t+dt) = I(t) * exp((sigma - gamma) * dt)
   ```

2. **Diffusion step:** Implicit with tridiagonal solve
   ```cpp
   (I - dt*D*Laplacian) * X(t+dt) = X(t)
   ```

This preserves stability for larger dt while remaining second-order accurate.

### Visualization Best Practices

1. **Colormap choice:**
   - Use **viridis** (default): colorblind-friendly, perceptually uniform
   - **Plasma** for high-contrast regions (predator hotspots)
   - **Thermal** for intuitive "hot/cold" interpretation

2. **Animation frame rate:**
   - Export every 10-50 steps (depending on model time scale)
   - Creates smooth 30 fps animations at 300-1500x speedup

3. **Statistical summary:**
   - Track mean, variance, max of each field over time
   - Detect phase transitions (e.g., variance drops → pattern locks)

### Validation & Benchmarking

**Against analytical solutions:**
- Non-spatial SEIR: compare to ODE solver (e.g., SciPy)
- Wave speed: measure vs. theory v = √(2αD_P)

**Determinism checks:**
Run the same configuration twice with the same seed and compare the resulting checkpoint hashes. Matching hashes confirm deterministic replay behavior.

---

## Running Extended Examples

### Prerequisites

- World-Simulator built from source (see User Guide, Getting Started)
- Models compiled (`.simmodel` files in `models/`)

### Quick Start

These example models are conceptual references. To try them in the current project, open the GUI or runtime shell, select a compatible `.simmodel` package, set the grid and seed, and then run the model for the desired step count.

If you want a minimal reproducibility check, repeat the same configuration twice and verify that the resulting checkpoint hashes match exactly.

### Expected Output

Both simulations should:
1. Complete without errors (exit code 0)
2. Report consistent state hashes if run twice with same seed
3. Save checkpoint to `.wscp` file
4. Display progress and timing information

### Troubleshooting

| Symptom | Cause | Solution |
|---------|-------|----------|
| Model not found | Path error | Select the model in the GUI or use `model select <id|path>` in the runtime shell, then confirm the path is correct |
| NaN/Inf values | Numerical instability | Reduce dt, reduce parameters, use implicit integrator |
| Out of memory | Grid too large | Reduce grid size (e.g., 64×64 instead of 256×256) |
| Slow execution | Debug build | Rebuild with `cmake -DCMAKE_BUILD_TYPE=Release` |

---

**For more guidance, see the User Guide and Developer Guide.**
