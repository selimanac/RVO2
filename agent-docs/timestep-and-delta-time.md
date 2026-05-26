# Timestep and Delta Time in RVO2

## `setTimeStep()` — What It Controls

`setTimeStep(dt)` sets how far the simulation advances per `doStep()` call:

- **Position integration:** `position += velocity * timeStep`
- **ORCA lookahead:** combined with `timeHorizon` to determine how far ahead collisions are predicted

---

## Fixed Timestep vs. `GetFrameTime()`

### Fixed timestep (e.g. `0.25`, `0.55`)

```cpp
sim->setTimeStep(0.25f);
```

- Simulation advances by the same amount every call, regardless of frame rate
- Velocity units are **"units per step"** — `maxSpeed` must be tuned to the fixed step
- Suitable for batch/headless simulations where wall-clock time does not matter
- Used in `raylib_circle.cpp` (`timeStep = 0.55`) because it is a pure simulation loop

### Variable timestep (`GetFrameTime()`)

```cpp
float dt = GetFrameTime();
sim->setTimeStep(dt);
```

- Simulation advances by the **actual elapsed time** each frame
- `velocity` is now in **world-units per second**, consistent with player movement:
  ```cpp
  playerPos += moveDir * PLAYER_SPEED * dt;  // same unit system
  ```
- Frame-rate independent: enemies move at the same real-world speed at 60 fps or 30 fps
- **Correct choice for interactive examples** where player and enemy speeds must match

---

## Stability Condition

ORCA becomes unstable when an agent moves more than its own radius in a single step:

```
maxSpeed × timeStep / radius  ≪  1
```

| maxSpeed | radius | timeStep | ratio  | safe? |
|----------|--------|----------|--------|-------|
| 200      | 20     | 0.016    | 0.16   | ✅    |
| 200      | 20     | 0.033    | 0.33   | ✅    |
| 200      | 20     | 0.050    | 0.50   | ✅ (margin) |
| 200      | 20     | 0.100    | 1.00   | ❌ unstable |

At low frame rates (< 20 fps) `dt` exceeds the safe margin. Clamp it:

```cpp
float dt = GetFrameTime();
if (dt <= 0.0f || dt > 0.05f)
    dt = 0.016f;          // fall back to 60 fps equivalent
sim->setTimeStep(dt);
```

- `dt <= 0.0f` — guards against a zero value on the first frame
- `dt > 0.05f` (< 20 fps) — prevents instability; falls back to 16 ms (60 fps)
- With `ENEMY_SPEED = 200` and `ENEMY_RADIUS = 20`: worst-case ratio is `200 × 0.05 / 20 = 0.5` — safely below 1

---

## Rule of Thumb

| Use case | Recommended timestep |
|----------|----------------------|
| Batch / headless simulation | Fixed value (e.g. `0.25`) |
| Interactive game loop (once per frame) | `GetFrameTime()` with clamp |
| Fixed-step game loop (e.g. 60 Hz tick) | `1.0f / 60.0f` |
