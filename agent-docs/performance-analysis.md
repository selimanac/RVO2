# RVO2 Performance Analysis

Findings from profiling and code analysis of the RVO2 library with 1250 agents
on the Circle scenario (`raylib_circle.cpp`).

---

## Baseline Measurements

Measured with `GetTime()` wall-clock timing in `raylib_circle.cpp`, build type `RelWithDebInfo`
(`-O2 -g -DNDEBUG`), Apple Silicon (ARM64), single-threaded.

| Agents | Sim step (ms) | Frame (ms) | FPS |
|--------|--------------|------------|-----|
| 250    | ~0.20        | ~12        | ~75 |
| 1250   | ~0.95        | ~12        | ~55 |

- **Sim time varies during the scenario.** Agents converge to the center mid-crossing, making
  the KD-tree unbalanced and queries slower. Peak sim time is expected at **1.5–2ms**.
- The displayed `sim: Xms` is a single-frame sample. A 60-frame rolling average + peak would
  give a more representative number.
- Frame time (~12ms) is dominated by raylib rendering, not the simulation.

---

## Simulation Parameters

```cpp
sim->setTimeStep(0.25f);
sim->setAgentDefaults(15.0f, 5U, 10.0f, 10.0f, 1.5f, 2.0f);
//                    neighborDist, maxNeighbors, timeHorizon, timeHorizonObst, radius, maxSpeed
```

### `timeStep = 0.25`
- **No per-step CPU cost.** Controls how much simulation time advances per `doStep()` call.
- Larger → fewer steps needed to complete a scenario (faster wall-clock completion).
- Larger → agents may overshoot / miss avoidance windows. Affects quality, not computation cost.

### `neighborDist = 15.0` — moderate CPU impact
- KD-tree query radius per agent. Each step, each agent queries for all agents within 15 world units.
- For 1250 agents on circle r=200 (circumference ~1257, ~1 unit apart): ~30 candidates per query.
- **Effect:** doubling `neighborDist` roughly doubles tree query time per agent.
- Minimum safe value depends on agent density and max speed. Too low → missed collisions.

### `maxNeighbors = 5` — **dominant performance parameter**
- Caps how many neighbors enter the ORCA linear program (`linearProgram2`).
- LP is **O(k²)**: each ORCA constraint is tested against all others.
- `agentNeighbors_` insertion maintains sorted order → O(k) per insert.

| maxNeighbors | Relative LP cost |
|---|---|
| 5  | 1×   |
| 10 | ~4×  |
| 20 | ~16× |

- Original `Circle.cc` uses `10U`. `raylib_circle.cpp` was tuned to `5U` — roughly 30–40% cheaper.
- Too few neighbors risks unsafe avoidance in dense scenarios.

### `timeHorizon = 10.0` — no CPU impact
- Scalar used as `invTimeHorizon = 1.0 / 10.0` in ORCA half-plane calculation.
- Larger → agents react to each other earlier (wider velocity obstacle cones).
- Affects simulation quality and smoothness, **not computation time**.

### `timeHorizonObst = 10.0` — zero cost in Circle example
- Applies only when static obstacles exist. Circle example has none — the obstacle loop
  never executes. Completely irrelevant to performance here.

### `radius = 1.5`, `maxSpeed = 2.0` — no CPU impact
- Scalars used in distance comparisons and velocity clamping. Not loop counters.

---

## Current Data Structure Problems

### Pointer-chasing (cache miss dominant cost)

```
RVOSimulator::agents_  →  std::vector<Agent*>
                                │    │    │
                              heap  heap  heap   (1250 scattered heap allocations)
```

Accessing all 1250 agents for tree rebuild = **1250 cache misses**. On ARM64, a cache miss
costs ~100 cycles → ~125,000 cycles just from pointer indirection before any computation.

### Per-agent dynamic allocations rebuilt every step

```cpp
// Cleared and repopulated on every doStep():
std::vector<std::pair<float, const Agent*>> agentNeighbors_;  // heap
std::vector<Line>                           orcaLines_;        // heap
```

No `malloc` after warmup (capacity is retained), but data lives in heap → cache miss on every
agent access during the ORCA computation loop.

### KD-tree full rebuild every step

```cpp
void RVOSimulator::doStep() {
    kdTree_->buildAgentTree();   // full O(n log n) rebuild — every step, no matter what
    for each agent: computeNeighbors + computeNewVelocity
    for each agent: update
}
```

The tree is sorted and partitioned from scratch each step. No incremental update.

### Agent struct size (~128 bytes, with heap pointers)

```cpp
std::vector<...> agentNeighbors_;   // 24 bytes (ptr + size + cap) → data elsewhere in heap
std::vector<...> obstacleNeighbors_;// 24 bytes
std::vector<Line> orcaLines_;       // 24 bytes
Vector2 position_, velocity_, ...   // 32 bytes
float/size_t fields                 // ~24 bytes
```

---

## `doStep()` Computation Cost Breakdown (1250 agents, 0.95ms)

| Phase | Complexity | Notes |
|---|---|---|
| `buildAgentTree()` | O(n log n) | Full rebuild, recursive partitioning, pointer-chasing |
| `computeNeighbors()` per agent | O(k · log n) | KD-tree query within neighborDist radius |
| `computeNewVelocity()` per agent | O(k²) | ORCA LP — dominant per-agent cost |
| `update()` per agent | O(n) | Apply new velocity, trivial |

For 1250 agents with k=5: ~65k LP operations + ~16k tree query ops + full O(n log n) rebuild.

---

## Rendering Bottleneck (raylib_circle.cpp)

The original frame drop (75fps → 55fps with 1250 agents) was **not the simulation** — it was
the rendering layer.

### Root cause: GPU batch flush on every agent

Raylib batches draw calls by GPU primitive type (GL_LINES vs GL_TRIANGLES). Each primitive
type switch forces a GPU batch flush. The original loop:

```cpp
for each of 1250 agents:
    DrawLineV(...)   // GL_LINES
    DrawCircleV(...) // GL_TRIANGLES  ← FLUSH on every agent
```

This caused **1250 GPU batch flushes per frame**.

### Fixes applied

| Fix | Change | Impact |
|---|---|---|
| Two-pass rendering | All velocity lines in one loop, then all circles | 1249/1250 batch flushes eliminated |
| RenderTexture cache | Formation ring + 1250 goal markers rendered once on reset, blitted as 1 draw call per frame | 1251 draw calls → 1 |
| Velocity toggle (`V` key) | Hide/show velocity lines | Saves 1250 `DrawLineV` calls |
| Timing overlay | `sim: Xms \| draw: Xms` in HUD | Separates sim vs render cost visibly |
| Lazy goal check | `reachedGoal()` only every 30 steps | Avoids 1250-iter loop most frames |

---

## Planned Optimizations (Future)

### Replace KD-tree with Box2D Dynamic Tree
- Box2D's `b2DynamicTree` uses a flat array of AABB nodes (no pointer chasing).
- Incremental `MoveProxy()` per moved agent instead of full rebuild.
- Self-balancing using surface area heuristics → stays efficient when agents cluster.
- **Expected gain:** ~20–30% on spatial query phase; better cache behavior overall.

### Flat array SoA layout

Replace `std::vector<Agent*>` (pointer-to-heap per agent) with contiguous flat arrays:

```c
float pos_x[MAX_AGENTS];      // all x positions — cache-line sequential
float pos_y[MAX_AGENTS];
float vel_x[MAX_AGENTS];
// ...
NeighborEntry neighbors[MAX_AGENTS * K_MAX];  // pre-allocated, fixed size
Line orcaLines[MAX_AGENTS * (K_MAX + 1)];     // pre-allocated
```

- Eliminates pointer indirection → CPU prefetcher works effectively.
- No heap allocation per agent per step.
- **Expected gain:** ~25–35% from cache miss reduction alone.

### Defold Job System for ORCA parallelism
- `computeNeighbors` + `computeNewVelocity` per agent are **embarrassingly parallel** —
  no dependencies between agents during this phase.
- Defold's `dmJobThread` replaces the OpenMP pragmas in `doStep()`.
- OpenMP will **not** be used (Apple Clang requires libomp; Defold job system integrates
  natively with the engine's threading model).
- **Expected gain with 4+ cores:** ~60–70% reduction in ORCA phase → total sim ~0.1ms.

### Realistic performance targets

| Configuration | Estimated sim time (1250 agents) |
|---|---|
| Current (pointer AoS, full KD-tree rebuild) | ~0.95ms (measured) |
| Flat arrays + pre-alloc + Box2D tree (single-thread) | ~0.3–0.4ms |
| + Defold job system (4 cores) | ~0.1ms |

---

## Build Configuration

- Build type: `RelWithDebInfo` (`-O2 -g -DNDEBUG`)
- Flags confirmed: `-O2 -g -DNDEBUG -std=c++11 -arch arm64`
- Default in root `CMakeLists.txt` is `Release`, but cache retains whatever was last set explicitly.
- Always pass `-DCMAKE_BUILD_TYPE=RelWithDebInfo` explicitly for reproducibility.
- **OpenMP:** `ENABLE_OPENMP=ON` exists in CMake but will not be used — see above.
