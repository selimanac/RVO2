# Priority and Responsibility ORCA Plan

This document plans a future update for RTS-style movement behavior in this
project: priority / responsibility ORCA.

The goal is to keep the ORCA solver structure, but change how much of each
pairwise avoidance correction each agent is responsible for. This lets moving
units avoid idle units without pushing them around, and lets heavier / higher
priority units influence lighter units more strongly.

## Current Behavior

In `src/Agent.cc`, agent-agent ORCA constraints are generated in
`Agent::computeNewVelocity(float timeStep)`.

For every neighboring agent, the code computes a correction vector `u` and then
creates this ORCA line:

```cpp
line.point = velocity_ + 0.5F * u;
```

The `0.5F` means equal responsibility:

- this agent takes half of the required velocity correction
- the other agent is expected to take the other half when it computes its own
  velocity

That is ideal for symmetric crowd simulation, but it looks wrong for RTS units.
An idle unit with preferred velocity zero can still move because ORCA is asking
it to share collision-avoidance responsibility.

## Desired Behavior

For RTS-style movement:

- Moving selected units should move around non-selected idle units.
- Idle units should mostly stay in place.
- Moving units should still avoid each other smoothly.
- Large / high-weight units should be able to push through or dominate small /
  low-weight units.
- Low-weight units should take more avoidance responsibility around high-weight
  units.

## Proposed Concept

Replace the fixed `0.5F` split with a per-pair responsibility factor:

```cpp
line.point = velocity_ + responsibility * u;
```

`responsibility` means: how much of the required correction this agent takes for
this specific pair.

Examples:

| This agent | Other agent | This responsibility |
|---|---:|---:|
| moving | moving | `0.5` |
| moving | idle blocker | `1.0` |
| idle blocker | moving | `0.0` |
| heavy moving | light moving | `< 0.5` |
| light moving | heavy moving | `> 0.5` |

Important: for each pair, both agents' responsibilities should ideally add to
`1.0`.

## Suggested Agent Data

Add these fields to `Agent` in `src/Agent.h`:

```cpp
float avoidanceWeight_;
float avoidancePriority_;
bool  isKinematic_;
bool  isAvoidanceLocked_;
```

Suggested meanings:

- `avoidanceWeight_`: physical/social weight. Higher values take less
  correction; lower values yield more.
- `avoidancePriority_`: gameplay priority. Useful for selected units, command
  groups, heroes, vehicles, etc.
- `isKinematic_`: agent is externally controlled or intentionally pinned.
- `isAvoidanceLocked_`: agent should not move due to ORCA correction. Idle RTS
  units can use this.

Minimum viable version:

```cpp
float avoidanceWeight_;
bool  isAvoidanceLocked_;
```

Defaults:

```cpp
avoidanceWeight_ = 1.0F;
avoidancePriority_ = 0.0F;
isKinematic_ = false;
isAvoidanceLocked_ = false;
```

Weights must be positive. Clamp invalid weights to a small positive value.

## Responsibility Function

Add a helper in `Agent.cc`:

```cpp
float Agent::computeResponsibility(const Agent* other) const;
```

Basic RTS version:

```cpp
float Agent::computeResponsibility(const Agent* other) const
{
    if (isAvoidanceLocked_ && !other->isAvoidanceLocked_)
    {
        return 0.0F;
    }

    if (!isAvoidanceLocked_ && other->isAvoidanceLocked_)
    {
        return 1.0F;
    }

    return 0.5F;
}
```

Weighted version:

```cpp
float Agent::computeResponsibility(const Agent* other) const
{
    if (isAvoidanceLocked_ && !other->isAvoidanceLocked_)
    {
        return 0.0F;
    }

    if (!isAvoidanceLocked_ && other->isAvoidanceLocked_)
    {
        return 1.0F;
    }

    const float selfWeight = std::max(avoidanceWeight_, RVO_EPSILON);
    const float otherWeight = std::max(other->avoidanceWeight_, RVO_EPSILON);

    return otherWeight / (selfWeight + otherWeight);
}
```

Why this formula works:

- If both weights are `1`, responsibility is `0.5`.
- If this agent is heavy and the other is light, this responsibility is small.
- If this agent is light and the other is heavy, this responsibility is large.

Example:

```text
selfWeight = 10
otherWeight = 1
responsibility = 1 / 11 = 0.09
```

The heavy agent takes only 9% of the correction. The light agent computes its
own constraint with:

```text
selfWeight = 1
otherWeight = 10
responsibility = 10 / 11 = 0.91
```

So the light agent takes 91% of the correction.

## Priority Extension

Priority can be mixed into effective weight:

```cpp
float Agent::effectiveAvoidanceWeight() const
{
    return std::max(avoidanceWeight_ + avoidancePriority_, RVO_EPSILON);
}
```

Or use multiplication:

```cpp
return std::max(avoidanceWeight_ * (1.0F + avoidancePriority_), RVO_EPSILON);
```

Recommended start:

```cpp
effectiveWeight = avoidanceWeight_;
```

Add priority later only if weight alone is not enough.

## Required Code Changes

### `src/Agent.h`

Add private fields:

```cpp
float avoidanceWeight_;
bool  isAvoidanceLocked_;
```

Add private helper:

```cpp
float computeResponsibility(const Agent* other) const;
```

`RVOSimulator` is already a friend class, so public setters can be exposed
through the simulator.

### `src/Agent.cc`

Initialize fields in `Agent::Agent()`:

```cpp
, avoidanceWeight_(1.0F)
, isAvoidanceLocked_(false)
```

Add `computeResponsibility()`.

Change:

```cpp
line.point = velocity_ + 0.5F * u;
```

to:

```cpp
const float responsibility = computeResponsibility(other);
line.point = velocity_ + responsibility * u;
```

This is the central implementation change.

### `src/RVOSimulator.h`

Add public API:

```cpp
void setAgentAvoidanceWeight(std::size_t agentNo, float weight);
float getAgentAvoidanceWeight(std::size_t agentNo) const;

void setAgentAvoidanceLocked(std::size_t agentNo, bool locked);
bool getAgentAvoidanceLocked(std::size_t agentNo) const;
```

Optional:

```cpp
void setAgentAvoidancePriority(std::size_t agentNo, float priority);
float getAgentAvoidancePriority(std::size_t agentNo) const;
```

### `src/RVOSimulator.cc`

Implement the setters/getters by reading and writing the corresponding `Agent`
fields.

Clamp weight:

```cpp
agents_[agentNo]->avoidanceWeight_ = std::max(weight, RVO_EPSILON);
```

## How RTS Movement Should Use It

In `examples/raylib_movement.cpp`, each frame:

```cpp
if (units[i].hasGoal)
{
    sim->setAgentAvoidanceLocked(units[i].simIdx, false);
    sim->setAgentAvoidanceWeight(units[i].simIdx, 1.0f);
    sim->setAgentMaxSpeed(units[i].simIdx, UNIT_SPEED);
}
else
{
    sim->setAgentAvoidanceLocked(units[i].simIdx, true);
    sim->setAgentAvoidanceWeight(units[i].simIdx, 10.0f);
    sim->setAgentPrefVelocity(units[i].simIdx, RVO::Vector2());
}
```

This tells moving units to take full responsibility against idle units. Idle
units stay much more stable.

For big units:

```cpp
sim->setAgentAvoidanceWeight(tankAgent, 8.0f);
sim->setAgentAvoidanceWeight(infantryAgent, 1.0f);
```

Infantry will do most of the yielding when near a tank.

## Kinematic Locking

If `isAvoidanceLocked_` only changes responsibility, an idle unit may still get
a nonzero ORCA velocity due to other constraints or max-speed solving.

For truly pinned units, add a second behavior in scenario code:

```cpp
if (!units[i].hasGoal)
{
    sim->setAgentPrefVelocity(units[i].simIdx, RVO::Vector2());
    sim->setAgentVelocity(units[i].simIdx, RVO::Vector2());
    sim->setAgentMaxSpeed(units[i].simIdx, 0.0f);
}
```

When it starts moving again:

```cpp
sim->setAgentMaxSpeed(units[i].simIdx, UNIT_SPEED);
```

Long term, this can become a simulator-level `setAgentKinematic()` API.

## Possible Responsibility Modes

Consider supporting modes:

```cpp
enum class AvoidanceMode {
    Reciprocal,   // default 0.5 split or weighted split
    Locked,       // agent does not take ORCA correction
    Kinematic,    // externally controlled / max speed zero when idle
    Disabled      // ignored by ORCA, not recommended for normal units
};
```

For this project, start with fields instead of an enum. Add an enum only if the
states become confusing.

## Edge Cases

### Both Agents Locked

If both are locked, responsibility can be `0.5` or `0.0`.

Recommended:

```cpp
if (isAvoidanceLocked_ && other->isAvoidanceLocked_)
{
    return 0.5F;
}
```

They should not be moving anyway, so this rarely matters.

### Weight Difference Too Large

A massive ratio like `1000` vs `1` can make small units react aggressively.
Clamp responsibility:

```cpp
return std::min(0.95F, std::max(0.05F, responsibility));
```

Do not clamp locked-vs-moving cases if you want true blocker behavior.

### Deadlocks

Priority ORCA can make low-priority agents over-yield and get stuck. If this
happens:

- reduce weight differences
- add local steering / pathfinding around groups
- give waiting units a small side bias
- use formation slots instead of one shared target

### Overlapping Starts

Priority ORCA does not fix bad initial overlaps by itself. Spawn units with
valid spacing or add a separate overlap-resolution step.

## Testing Plan

### Unit-Level Tests

Add tests around `computeResponsibility()`:

- equal weights return `0.5`
- self heavy / other light returns below `0.5`
- self light / other heavy returns above `0.5`
- self locked / other moving returns `0.0`
- self moving / other locked returns `1.0`

### Scenario Tests

Manual visual tests:

1. Spawn idle units.
2. Select one moving unit and command it through the idle group.
3. Verify idle units mostly stay still.
4. Verify moving unit routes around them.
5. Give one unit high weight and command it through low-weight units.
6. Verify low-weight units yield more than the high-weight unit.

### Regression Checks

Run existing examples:

- `raylib_circle`
- `raylib_blocks`
- `raylib_movement`

Default weights should preserve current behavior when all agents have:

```cpp
avoidanceWeight_ = 1.0F;
isAvoidanceLocked_ = false;
```

## Recommended Implementation Order

1. Add `avoidanceWeight_` and `isAvoidanceLocked_` to `Agent`.
2. Add `computeResponsibility()` with equal-weight behavior preserving `0.5`.
3. Replace the hardcoded `0.5F` in `Agent::computeNewVelocity()`.
4. Add simulator setters/getters.
5. Update `raylib_movement.cpp` so idle units are locked and moving units are
   unlocked.
6. Add optional high-weight test values to validate big-unit behavior.
7. Tune responsibility clamping only after visual testing.

## Summary

Priority / responsibility ORCA is a small but powerful extension:

```cpp
line.point = velocity_ + responsibility * u;
```

The default case remains standard ORCA with `responsibility = 0.5`. RTS-style
behavior comes from making idle or heavier agents take less responsibility and
moving or lighter agents take more responsibility.
