# Agent Arrival Design

This document describes a suggested way to add reusable arrival behavior to the
RVO2 `Agent` implementation without changing the ORCA collision-avoidance
solver itself.

The core idea is to keep ORCA responsible for local collision avoidance and add
arrival handling as a small layer that decides each agent's preferred velocity.
ORCA should still receive `prefVelocity_` and compute `newVelocity_` exactly as
it does today.

## Problem

Examples currently decide arrival in scenario code. That works for small demos,
but it causes repeated bugs in crowded scenes:

- A single shared goal point does not work well for many radius-based agents.
- A huge goal radius can trigger completion while agents still look far away.
- A tiny radius can never trigger if many agents are trying to occupy the same
  point.
- Agents that reach the goal can jitter because they continue steering toward
  the exact point while ORCA pushes them around.
- Polling all positions every N frames can miss an agent that briefly entered
  the arrival radius and was then pushed away.

The proper model is:

1. Each agent may have a movement goal.
2. Arrival can be sticky once the goal is reached.
3. Arrived agents should request zero preferred velocity.
4. Scenario code should choose appropriate goal placement and radius policy.

## What Should Live in `Agent`

Good candidates for `Agent`:

- Optional goal position.
- Whether the agent has a goal.
- Whether the agent has arrived.
- Stop radius / reached radius.
- Desired movement speed toward the goal.
- A helper that updates `prefVelocity_` from the goal before ORCA runs.

Things that should not be mixed into ORCA:

- The linear-programming functions.
- ORCA line construction.
- Neighbor search.
- Scenario-wide completion policy.
- Drawing goal markers.

ORCA does not need to know why `prefVelocity_` has a certain value. It only
needs a preferred velocity to optimize against.

## Suggested Data Members

Add these fields to `Agent` in `src/Agent.h`:

```cpp
Vector2 goal_;
bool    hasGoal_;
bool    arrived_;
bool    stickyArrival_;
float   desiredSpeed_;
float   stopRadius_;
float   reachedRadius_;
```

Suggested defaults in `Agent::Agent()`:

```cpp
, hasGoal_(false)
, arrived_(false)
, stickyArrival_(true)
, desiredSpeed_(0.0F)
, stopRadius_(0.0F)
, reachedRadius_(0.0F)
```

Use `reachedRadius_` for deciding whether the agent has arrived. Use
`stopRadius_` for deciding when the agent should stop steering. In many
scenarios they can be the same value, but keeping both allows hysteresis.

## Suggested Private Agent Functions

### `Agent::updatePreferredVelocityFromGoal()`

Purpose: convert goal state into `prefVelocity_` before `computeNewVelocity()`.

Suggested behavior:

```cpp
void Agent::updatePreferredVelocityFromGoal()
{
    if (!hasGoal_)
    {
        return;
    }

    if (stickyArrival_ && arrived_)
    {
        prefVelocity_ = Vector2();
        return;
    }

    const Vector2 toGoal = goal_ - position_;
    const float distSq = absSq(toGoal);
    const float reachedRadiusSq = reachedRadius_ * reachedRadius_;

    if (distSq <= reachedRadiusSq)
    {
        arrived_ = true;
        prefVelocity_ = Vector2();
        return;
    }

    const float stopRadiusSq = stopRadius_ * stopRadius_;

    if (distSq <= stopRadiusSq)
    {
        prefVelocity_ = Vector2();
        return;
    }

    prefVelocity_ = normalize(toGoal) * desiredSpeed_;
}
```

Important guard: only call `normalize(toGoal)` after checking the distance is
outside the stop/reached radius.

### `Agent::hasArrived() const`

Purpose: expose sticky per-agent arrival state.

```cpp
bool Agent::hasArrived() const
{
    return arrived_;
}
```

### `Agent::clearGoal()`

Purpose: return the agent to manual `setAgentPrefVelocity()` control.

```cpp
void Agent::clearGoal()
{
    hasGoal_ = false;
    arrived_ = false;
}
```

## Suggested Public Simulator API

Because `Agent` is private to the library, expose arrival behavior through
`RVOSimulator`.

### `setAgentGoal(...)`

```cpp
void setAgentGoal(std::size_t agentNo,
                  const Vector2& goal,
                  float desiredSpeed,
                  float reachedRadius,
                  float stopRadius,
                  bool stickyArrival = true);
```

Suggested implementation:

```cpp
Agent* agent = agents_[agentNo];
agent->goal_ = goal;
agent->desiredSpeed_ = desiredSpeed;
agent->reachedRadius_ = reachedRadius;
agent->stopRadius_ = stopRadius;
agent->stickyArrival_ = stickyArrival;
agent->hasGoal_ = true;
agent->arrived_ = false;
```

### `clearAgentGoal(...)`

```cpp
void clearAgentGoal(std::size_t agentNo);
```

Clears goal mode so caller code can set `prefVelocity_` manually again.

### `getAgentGoal(...)`

```cpp
const Vector2& getAgentGoal(std::size_t agentNo) const;
```

Useful for debug drawing. If goals are enabled, draw these per-agent goals
instead of drawing unrelated group-level markers.

### `getAgentReachedGoal(...)`

```cpp
bool getAgentReachedGoal(std::size_t agentNo) const;
```

Returns `arrived_`. This is the value scenario code should use for completion
when sticky arrival is enabled.

### `getNumAgentsReachedGoal()`

```cpp
std::size_t getNumAgentsReachedGoal() const;
```

Convenience helper for HUDs and completion checks.

## Where to Call the Goal Helper

Call `Agent::updatePreferredVelocityFromGoal()` inside `RVOSimulator::doStep()`
before `computeNeighbors()` and `computeNewVelocity()`:

```cpp
for (int i = 0; i < static_cast<int>(agents_.size()); ++i) {
    agents_[i]->updatePreferredVelocityFromGoal();
    agents_[i]->computeNeighbors(kdTree_);
    agents_[i]->computeNewVelocity(timeStep_);
}
```

This preserves the current ORCA flow:

1. Preferred velocity is prepared.
2. ORCA constraints are generated.
3. ORCA computes `newVelocity_`.
4. `update()` applies velocity and position.

## Completion Policy

Scenario code should avoid checking raw distance directly if sticky arrival is
enabled. Instead:

```cpp
bool allReached = true;

for (std::size_t i = 0; i < sim->getNumAgents(); ++i)
{
    if (!sim->getAgentReachedGoal(i))
    {
        allReached = false;
        break;
    }
}
```

Or use `getNumAgentsReachedGoal()`:

```cpp
bool allReached = sim->getNumAgentsReachedGoal() == sim->getNumAgents();
```

This avoids missing agents that briefly entered the radius and were pushed out
before the next completion check.

## Scenario Guidelines

### One Agent to One Point

Use a small radius:

```cpp
reachedRadius = 1.0F * radius;
stopRadius = 1.0F * radius;
stickyArrival = true;
```

### Many Agents Swapping Places

Give each agent its own goal slot, usually the mirrored start position:

```cpp
goal = -startPosition;
reachedRadius = 2.0F * radius;
stopRadius = 2.0F * radius;
stickyArrival = true;
```

This is the best model for Blocks-style examples. It avoids forcing many agents
to occupy one point.

### Many Agents Entering the Same Region

Use a goal region instead of a point. This should usually stay in scenario code
or be represented as a higher-level API:

```cpp
bool isInsideGoalRegion(const Vector2& position);
```

Examples:

- Circle region: distance from center <= region radius.
- Rectangle region: position inside bounds.
- Crossing-line region: agent has crossed a finish line.

For this scenario, `Agent` can still store a steering target, but completion
should be based on the region.

### Chasing a Moving Target

Do not use sticky arrival unless the agent should permanently stop after first
contact. For game enemies chasing a player:

```cpp
stickyArrival = false;
reachedRadius = attackRadius;
stopRadius = attackRadius;
```

Update the goal each frame to the target's current position.

### Endless Crowd Flow

Do not use global "all reached" completion. Use scenario-specific conditions:

- Agent crossed an exit line.
- Agent left the map bounds.
- A fixed simulation time elapsed.
- A fixed number of agents completed.

## Recommended Defaults

For an agent with radius `r`:

```cpp
stopRadius = 2.0F * r;
reachedRadius = 2.0F * r;
desiredSpeed = maxSpeed;
stickyArrival = true;
```

If agents visually stop too early, reduce to `1.5F * r`. If dense groups keep
nudging arrived agents out of place, increase to `3.0F * r`.

## Important Warnings

- Do not use `1200` as an arrival radius unless the goal region is intentionally
  enormous. It means "arrived within 1200 world units", which will trigger far
  away from a point target.
- Do not send dozens of radius-based agents to one exact point and expect all of
  them to satisfy a tiny distance threshold.
- Draw the same goals that the simulation checks. If agents use per-agent goals,
  debug draw per-agent goals, not only shared group markers.
- Keep manual `setAgentPrefVelocity()` available. Goal mode should be optional,
  not a replacement for the existing API.
