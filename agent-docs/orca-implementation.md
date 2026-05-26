# ORCA Implementation Notes

This project uses ORCA, Optimal Reciprocal Collision Avoidance, as the local
collision-avoidance algorithm inside the RVO2 simulation step. ORCA is not a
separate public module here. It is implemented mainly in `src/Agent.cc`, with
`src/RVOSimulator.cc` orchestrating each step and `src/KdTree.cc` supplying the
neighbor queries that limit how many constraints each agent has to consider.

At each simulation step, every agent chooses a new velocity that is as close as
possible to its preferred velocity while satisfying half-plane velocity
constraints generated from nearby agents and obstacles.

## High-Level Flow

`RVOSimulator::doStep()` is the entry point for ORCA work:

```cpp
kdTree_->buildAgentTree();

for each agent:
    agent->computeNeighbors(kdTree_);
    agent->computeNewVelocity(timeStep_);

for each agent:
    agent->update(timeStep_);
```

The important detail is that all agents compute their `newVelocity_` from the
same previous positions and velocities before any positions are updated. This
keeps the reciprocal avoidance step symmetric and safe to parallelize with
OpenMP.

## ORCA Constraints

ORCA represents each avoidance requirement as a `Line`:

- `Line::point` is a point on the boundary of the velocity half-plane.
- `Line::direction` is the directed boundary line.
- The permissible velocities are on the left side of the line, as documented by
  `RVOSimulator::getAgentORCALine()`.

An agent stores these constraints in `Agent::orcaLines_` during
`Agent::computeNewVelocity()`.

Obstacle constraints are added first. Agent constraints are added second. The
split matters because `linearProgram3()` preserves the obstacle constraints when
repairing failures caused by later agent constraints.

## Function Descriptions

### `RVOSimulator::doStep()`

Location: `src/RVOSimulator.cc`

Runs one full simulation tick. It rebuilds the agent k-D tree, computes
neighbors and ORCA velocities for every agent, applies the resulting velocities
to positions, and advances `globalTime_`.

ORCA usage:

- Calls `KdTree::buildAgentTree()` so agent-neighbor searches use current
  positions.
- Calls `Agent::computeNeighbors()` to collect nearby obstacles and agents.
- Calls `Agent::computeNewVelocity(timeStep_)` to build ORCA lines and solve for
  the next velocity.
- Calls `Agent::update(timeStep_)` only after every agent has its next velocity.

### `Agent::computeNeighbors(const KdTree *kdTree)`

Location: `src/Agent.cc`

Builds the local neighborhood used by ORCA for one agent.

Obstacle neighbors are searched within:

```cpp
timeHorizonObst_ * maxSpeed_ + radius_
```

Agent neighbors are searched within `neighborDist_`, capped to
`maxNeighbors_`. The method clears the previous neighbor lists before each
search, so ORCA constraints are regenerated from scratch every step.

### `Agent::computeNewVelocity(float timeStep)`

Location: `src/Agent.cc`

This is the core ORCA implementation. It creates all velocity constraints for
the current agent and then solves for `newVelocity_`.

The method does three main jobs:

1. Builds obstacle ORCA lines from `obstacleNeighbors_`.
2. Builds agent ORCA lines from `agentNeighbors_`.
3. Runs the linear-programming solver to choose the allowed velocity closest to
   `prefVelocity_`, limited by `maxSpeed_`.

For obstacles, it projects the current velocity onto the relevant part of the
obstacle velocity obstacle: vertex cutoff circles, segment cutoff line, left
leg, or right leg. It also handles convex and non-convex obstacle vertices.

For agents, it computes pairwise reciprocal constraints. When agents are not
currently colliding, the constraint uses `timeHorizon_`. When they are already
overlapping or touching, it uses the current `timeStep` to create a stronger
separation constraint. The line point is:

```cpp
velocity_ + 0.5F * u
```

The `0.5F` factor is the reciprocal part of ORCA: this agent takes half of the
required velocity correction and the other agent is expected to take the other
half when it computes its own constraint.

### `Agent::update(float timeStep)`

Location: `src/Agent.cc`

Applies the result of ORCA:

```cpp
velocity_ = newVelocity_;
position_ += velocity_ * timeStep;
```

This function does not perform collision checks. It trusts that
`computeNewVelocity()` produced a velocity inside the ORCA feasible region.

### `Agent::insertAgentNeighbor(const Agent *agent, float &rangeSq)`

Location: `src/Agent.cc`

Adds a nearby agent to `agentNeighbors_` if it is within the current squared
search range and is not the same object as `this`.

The list is kept sorted by squared distance. Once the list reaches
`maxNeighbors_`, the farthest accepted neighbor becomes the new search range.
This lets the k-D tree prune future candidates more aggressively.

### `Agent::insertObstacleNeighbor(const Obstacle *obstacle, float rangeSq)`

Location: `src/Agent.cc`

Adds an obstacle segment to `obstacleNeighbors_` if the closest point on the
segment is within the squared obstacle search range.

The list is sorted by squared distance so nearer obstacle constraints are
processed first when ORCA lines are generated.

### `linearProgram1(...)`

Location: anonymous namespace in `src/Agent.cc`

Solves a one-dimensional optimization problem along one ORCA line while
respecting all earlier lines and the circular `maxSpeed_` bound.

It is called by `linearProgram2()` when a proposed velocity violates a
constraint. If the selected line has no feasible interval inside the speed
circle and previous constraints, it returns `false`.

### `linearProgram2(...)`

Location: anonymous namespace in `src/Agent.cc`

Solves the two-dimensional velocity selection problem:

- Start from `prefVelocity_`, clamped to the `maxSpeed_` circle.
- Walk through every ORCA line.
- If the current result violates a line, call `linearProgram1()` to find the
  best point on that line.

It returns `lines.size()` on success. If it cannot satisfy a line, it returns
the index of the failed line so `linearProgram3()` can repair the result.

### `linearProgram3(...)`

Location: anonymous namespace in `src/Agent.cc`

Repairs a result from `linearProgram2()` when later constraints make the
velocity infeasible. It projects the problem into new constraint lines and
re-solves while preserving the obstacle constraints at the front of the list.

This is why `computeNewVelocity()` records `numObstLines` before adding
agent-agent ORCA lines.

### `KdTree::buildAgentTree()`

Location: `src/KdTree.cc`

Builds the spatial acceleration structure used to find nearby agents. This is
called every simulation step because agents move every step.

### `KdTree::computeAgentNeighbors(Agent *agent, float &rangeSq)`

Location: `src/KdTree.cc`

Queries the agent k-D tree and calls `Agent::insertAgentNeighbor()` for
candidates inside the current range. `rangeSq` may shrink as the agent accepts
nearer neighbors and fills its `maxNeighbors_` quota.

### `KdTree::computeObstacleNeighbors(Agent *agent, float rangeSq)`

Location: `src/KdTree.cc`

Queries the obstacle tree and calls `Agent::insertObstacleNeighbor()` for
nearby obstacle segments. Unlike the agent range, this range is passed by value
and does not shrink during the search.

### `RVOSimulator::processObstacles()`

Location: `src/RVOSimulator.cc`

Builds the obstacle k-D tree by calling `KdTree::buildObstacleTree()`. Static
obstacles only affect ORCA after this function has been called. Obstacles added
after processing are not considered until the obstacle tree is rebuilt.

### ORCA Inspection Helpers

Location: `src/RVOSimulator.cc` and `src/RVOSimulator.h`

These public methods expose the internal ORCA state for diagnostics or
visualization:

- `getAgentNumORCALines(agentNo)` returns how many constraints were generated
  for the agent in the most recent velocity computation.
- `getAgentORCALine(agentNo, lineNo)` returns a specific ORCA line.
- `getAgentNumAgentNeighbors(agentNo)` and
  `getAgentAgentNeighbor(agentNo, neighborNo)` expose the nearby agents that
  contributed agent-agent constraints.
- `getAgentNumObstacleNeighbors(agentNo)` and
  `getAgentObstacleNeighbor(agentNo, neighborNo)` expose the nearby obstacle
  segments that contributed obstacle constraints.

## Parameters That Shape ORCA Behavior

### `prefVelocity_`

The velocity the caller wants the agent to take if there were no collisions.
ORCA treats it as the optimization target, not a command. The final velocity may
be different if constraints require avoidance.

Set through:

```cpp
RVOSimulator::setAgentPrefVelocity(agentNo, velocity)
```

### `maxSpeed_`

The radius of the velocity-space circle used by the linear programs. ORCA will
not choose a velocity longer than this value.

### `radius_`

The physical agent radius used for collision predictions. Agent-agent
constraints use the sum of both agents' radii.

### `neighborDist_`

The maximum center-to-center distance for considering other agents. Agents
outside this range produce no ORCA constraints, even if they might collide later.

### `maxNeighbors_`

The maximum number of agent neighbors retained for ORCA. Lower values reduce
work but can remove constraints needed for dense crowds.

### `timeHorizon_`

The lookahead time for avoiding other agents. Larger values make agents react
earlier but constrain their velocity choices more strongly.

### `timeHorizonObst_`

The lookahead time for avoiding static obstacles. It must be positive because
`computeNewVelocity()` computes its reciprocal.

### `timeStep_`

The integration step for `update()`. It is also used by ORCA when agents are
already colliding, where the algorithm switches from `timeHorizon_` to
`timeStep` to generate a more immediate separation constraint.

## Practical Notes

- ORCA prevents future collisions; it is not designed to magically fix badly
  overlapping initial positions.
- `setAgentPrefVelocity()` should usually be called before every `doStep()` so
  the optimizer has a current target velocity.
- Static obstacles require `processObstacles()` before stepping.
- `timeHorizon_`, `timeHorizonObst_`, and `timeStep_` should be positive.
- In dense scenes, increasing `neighborDist_` without also increasing
  `maxNeighbors_` may not help because the accepted neighbor list is still
  capped.
