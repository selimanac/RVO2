# raylib_follow Bottleneck Findings

Date: 2026-05-26

## Summary

`raylib_follow.cpp` is currently bottlenecked less by the raw number of agents and more by dense local avoidance work. The strongest indicator is the neighbor HUD measurement:

```text
2300 units: neighbors avg/max ~= 15.5/24, sim ~= 4.5ms
2300 units after cap reduction: neighbors avg/max ~= 13.3/20
```

This means many agents are at or near their configured `maxNeighbors`, so each simulation step is solving a large number of ORCA constraints. The per-frame kd-tree rebuild is still a cost, but the current slowdown is mainly caused by dense neighbor processing and velocity solving.

## Current Scenario Characteristics

`raylib_follow.cpp` uses mixed-size agents:

```cpp
unit_sizes    = { 20, 30, 40, 60 };
unit_speeds   = { 450, 400, 340, 280 };
max_neighbors = { 8, 10, 16, 20 };
```

Enemy `neighborDist` is radius-based:

```cpp
neighborDist = unit_radius * 8.0f + 150.0f;
```

That gives:

```text
radius 20 -> neighborDist 310
radius 30 -> neighborDist 390
radius 40 -> neighborDist 470
radius 60 -> neighborDist 630
```

Large agents therefore query a much wider region, collect more candidates, and often hit their neighbor cap.

## Primary Findings

### 1. Density dominates cost

Reducing the usable area from larger values to smaller values increases density nonlinearly. For example, reducing linear space from `3000` to `1700` increases density by roughly:

```text
(3000 / 1700)^2 ~= 3.1x
```

So even if agent count is unchanged, each agent sees more candidates and fills its neighbor list more often.

### 2. Average neighbor count is the best visible bottleneck signal

The measured `avgNeighbors = 13.3` means the solver is doing substantial work per agent. This is not a trivial broadphase case anymore. The system is close to saturated local avoidance.

When the average is high, cost increases in several places:

- kd-tree range queries prune less effectively in dense regions.
- `Agent::insertAgentNeighbor()` does sorted insertion into the bounded neighbor list.
- `Agent::computeNewVelocity()` builds ORCA lines for all selected neighbors.
- `linearProgram2()` and sometimes `linearProgram3()` solve a larger velocity constraint set.

### 3. Full kd-tree rebuild is not the only bottleneck

`RVOSimulator::doStep()` rebuilds the agent kd-tree every frame:

```cpp
kdTree_->buildAgentTree();
```

This is a real cost and worth optimizing in a rewrite. However, the observed slowdown after increasing density points more strongly at neighbor processing and ORCA solving. If rebuild were the dominant cost, reducing area would not hurt this much by itself.

### 4. Visual quality requires enough neighbors

Lowering all `maxNeighbors` to `5` improves performance only a little and causes poor behavior, especially for large agents. This is expected: large agents physically interact with more surrounding agents and need a wider local constraint set.

The current cap set `{8, 10, 16, 20}` is a more reasonable compromise than uniform low caps.

### 5. Stability fixes improved visuals but can increase real solver work

The following fixes improved behavior:

- fixed RVO timestep: `1.0f / 60.0f`
- player position updated before enemy preferred velocities and `doStep()`
- removed post-step enemy hard-stop velocity override
- reduced `timeHorizon` from extreme values back to `3.0f`
- lowered speeds and tuned neighbor caps

These changes make ORCA solve the actual current-frame crowd problem instead of fighting post-step corrections. This improves visual stability, but it can also increase constraint work because agents now participate more consistently in dense crowd negotiation.

## Hot Code Paths

The likely hot paths are:

```text
RVOSimulator::doStep()
  KdTree::buildAgentTree()
  Agent::computeNeighbors()
    KdTree::queryAgentTreeRecursive()
    Agent::insertAgentNeighbor()
  Agent::computeNewVelocity()
    ORCA line generation
    linearProgram2()
    linearProgram3()
  Agent::update()
```

The highest-value profiling split would be:

```text
preferred velocity setup
buildAgentTree
computeNeighbors
computeNewVelocity
update
draw
```

## Recommendations

### Short-term tuning

Keep the neighbor HUD. Track `avg/max` whenever changing formation radius, speed, or neighbor caps.

Useful targets:

```text
avgNeighbors <= 8   : cheap / easy case
avgNeighbors 8-12   : good practical range
avgNeighbors 12-16  : dense, solver-heavy
avgNeighbors > 16   : expensive, likely saturated
```

Try to keep the follow scenario near `10-12` average if possible.

### Data-oriented rewrite

A flat-array rewrite should help:

- replace `std::vector<Agent*>` with dense arrays
- store neighbor indices instead of `Agent*`
- use fixed-size neighbor buffers
- avoid per-agent vector allocation/growth
- keep ORCA line buffers preallocated
- make update loops easier to parallelize

This will reduce cache misses and allocation overhead, but it will not remove the algorithmic cost of high average neighbor counts.

### Spatial index alternatives

Candidate replacements for the current rebuilt kd-tree:

- uniform grid / spatial hash: likely best for homogeneous crowds
- dense cell-linked list: strong for flat arrays and SIMD/job systems
- Box2D dynamic tree: useful broadphase, especially for uneven or dynamic worlds
- flattened BVH/kd-tree: still general-purpose, but more complex

For this crowd-follow case, a uniform grid or dense cell-linked list is probably the first option to benchmark.

### Algorithmic improvements

The biggest production-style wins usually come from not running full ORCA at full fidelity for every agent:

- crowd LOD: update far/outer agents every 2nd or 4th frame
- simple steering for non-interacting agents
- full ORCA only for dense or near-player agents
- angular neighbor selection instead of pure nearest-neighbor caps
- lower precision / fewer neighbors for agents outside the visible focus area

### Behavior-side improvements

The follow-player behavior is inherently pressure-heavy because many agents target the same moving point. Slot/ring targeting was tested and had minimal impact in this version, so it was reverted. Other behavior-level options may still help:

- larger stop ring
- tangential flow around the player
- congestion-aware speed reduction
- limit number of active attackers/followers near the player
- let excess agents idle or orbit farther away

## Conclusion

The current bottleneck is dense ORCA constraint solving driven by high neighbor counts, not just the kd-tree rebuild. The kd-tree rebuild and pointer-heavy implementation are worth replacing, but measured neighbor saturation shows that the main cost is now the amount of local avoidance work requested from the solver.

The most promising path is a combination of:

```text
flat arrays + grid/cell broadphase
fixed-size neighbor buffers
parallel doStep loops
crowd LOD / cheaper updates for less important agents
continued tuning against avg/max neighbor HUD
```

