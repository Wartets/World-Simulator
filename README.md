# World-Simulator

Model-agnostic world simulation framework.

- 2D grid-based environment
- Multiple interchangeable models (A/B/C)
- Focus on realism, consistency, and experimentation

## Running the application

Build the project and launch `build/world_sim.exe`.

The executable now starts an interactive runtime session and stays open until you explicitly exit.

For a native graphical interface on Windows, launch `build/world_sim_gui.exe`.

The GUI uses an ImGui-based **single-cockpit control panel** over a full OpenGL viewport.

Core cockpit sections include:

- Info and Performance status badges
- Grid/configuration controls (seed, grid dimensions, tier, temporal policy)
- Session tools (start/stop/restart)
- Presets/profile operations (save/load/list)
- Simulation controls (step, run-until, pause/resume)
- Physics-adapted controls (Force Fields, Particle Properties, Constraints)
- Display tuning (zoom/pan/brightness/contrast/gamma/grid/boundary)
- Analysis and checkpoint workflow
- Accessibility controls (UI scale, font size, high contrast, keyboard navigation, focus indicators, reduced motion)

The in-panel event log records operation outputs, including deterministic hashes and diagnostics.

### Core interactive commands

- `help` — list all commands
- `dashboard` — show a compact operator dashboard (run identity, hashes, active config)
- `status` — show runtime status, hashes, and diagnostics summary
- `step [count]` — advance simulation deterministically by one or more steps
- `rununtil <step_index>` — advance until an absolute step target
- `runscript <path>` — execute a command script file (lines, `#` comments supported)
- `bench <steps>` — run a timed stepping benchmark and report throughput
- `pause` / `resume` — control runtime stepping
- `checkpoint <label>` — store an in-memory checkpoint
- `restore <label>` — reset state to a saved checkpoint
- `listcp` — list checkpoint labels currently held in memory
- `savecp <label> <path>` — persist checkpoint to disk (`.wscp` binary payload)
- `loadcp <label> <path>` — load a persisted checkpoint into memory
- `input <var> <x> <y> <value>` — queue manual scalar input patches
- `event <x> <y> <signal>` — queue manual event signal injection
- `summary <variable>` — compute field statistics (valid count, min, max, average)
- `fields` — list all variables present in the current snapshot
- `sample <variable> <x> <y>` — inspect one cell value for a variable
- `heatmap <variable> [w h]` — render a compact ASCII map for quick visual inspection
- `history [count]` — print recent interactive commands
- `savehistory <path>` — export full command history to a text file
- `metrics` — print observability counters
- `config` — show current launch configuration
- `preset` / `preset <name>` — list or apply predefined launch presets
- `profile list` / `profile save <name>` / `profile load <name>` — persistent runtime configuration profiles
- `set seed <u64>` / `set grid <w> <h>` / `set tier <A|B|C>` / `set temporal <uniform|phased|multirate>`
- `restart` — relaunch runtime with current configuration
- `stop` — stop active runtime
- `exit` — close the application

## Persistence workflow

Typical checkpoint persistence sequence:

1. `checkpoint quick`
2. `savecp quick build/checkpoints/quick.wscp`
3. Later session: `loadcp quick build/checkpoints/quick.wscp`
4. `restore quick`

## Persistent runtime configuration profiles

Profiles are stored in `profiles/*.wsprofile` and persist launch configuration values:

- seed
- grid width/height
- model tier
- temporal policy

Example sequence:

1. `set seed 2026`
2. `set grid 64 32`
3. `set tier C`
4. `set temporal multirate`
5. `profile save hi_fidelity`
6. Later: `profile load hi_fidelity`
7. `restart`

## Automation script example

- Example script: `scripts/ops_demo.wscli`
- Run from shell command stream or interactive prompt:
	- `runscript scripts/ops_demo.wscli`

## Session auditing

- Use `history` to inspect command chronology during a live run.
- Use `savehistory build/logs/session_history.txt` to persist an operator log for post-run analysis.

## Application code layout

Application shell code has been moved out of `src/main.cpp` into dedicated modules:

- `src/app/runtime_shell.cpp` — command dispatch and user interface flow
- `src/app/shell_support.cpp` — parsing, config builders, presets, summaries
- `src/app/checkpoint_io.cpp` — checkpoint binary persistence
- `src/app/profile_store.cpp` — persistent runtime profile storage

Public app headers are in `include/ws/app/`.

## Determinism and observability

Each session reports:

- `run_identity_hash` for run identity tracking
- `state_hash` for state consistency checks
- reproducibility class and per-step diagnostics

This enables direct deterministic replay checks across repeated runs with identical configuration.
