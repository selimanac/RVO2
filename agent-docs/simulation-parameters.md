# RVO2 Simulation Parameter Notes

## `setAgentDefaults` Signature

```cpp
sim->setAgentDefaults(neighborDist, maxNeighbors, timeHorizon, timeHorizonObst, radius, maxSpeed);
```

---

## `neighborDist`

How far away (center-to-center) another agent must be to be considered a neighbor and included in velocity computation. Agents beyond this distance are completely ignored.

### Formula

```cpp
neighborDist = maxSpeed * timeHorizon + 2.0f * radius;  // theoretically correct minimum
// or simpler heuristic:
neighborDist = 10.0f * radius;                          // matches original Circle.cc ratio
```

The formula answers: *"how far away can an agent be and still reach me before the time horizon expires, if both closing at max speed?"*

- Going **above** the formula is safe — just considers more agents per LP solve (slightly slower)
- Going **below** risks agents not seeing each other in time to avoid

### Example (`radius=30, maxSpeed=12, timeHorizon=10`)

```
neighborDist = 12 × 10 + 2 × 30 = 180   (formula)
neighborDist = 10 × 30         = 300   (heuristic — more conservative, current default)
```

---

## `timeHorizon`

How many seconds ahead the ORCA algorithm plans agent-to-agent avoidance. Larger values make agents start avoiding each other earlier.

---

## `timeHorizonObst`

How many seconds ahead obstacle avoidance is planned.

### With no obstacles in the scene

`timeHorizonObst` has **zero effect on simulation behavior** — the obstacle avoidance loop finds nothing and generates no ORCA lines.

However, the code computes `1.0F / timeHorizonObst_` unconditionally:

```cpp
const float invTimeHorizonObst = 1.0F / timeHorizonObst_;  // always executed
```

Setting it to `0` produces `+Infinity` (IEEE 754 float division). With no obstacles this infinity is never used, but it is still wrong by API contract. **Always set to a positive value** (e.g. `1.0f`) regardless of whether obstacles are present.

---

## `maxSpeed`

ORCA stability condition — agents must not move more than their radius per timestep:

```
maxSpeed × timeStep / radius ≪ 1
```

| Setup | Calculation | Status |
|---|---|---|
| Original Circle.cc (`maxSpeed=2, timeStep=0.25, radius=1.5`) | `2 × 0.25 / 1.5 = 0.33` | ✓ 33% radius/step |
| raylib_circle (`maxSpeed=12, timeStep=0.25, radius=30`) | `12 × 0.25 / 30 = 0.10` | ✓ 10% radius/step — more stable |

Speed of 10–15 is fine for `radius=30, timeStep=0.25`. There is no reason to lower it.

---

## `FORM_RADIUS` — Initial Circle Formation Radius

When placing `N` agents of `radius r` evenly on a circle, the arc spacing between adjacent agent centers is:

```
arc_spacing = 2π × FORM_RADIUS / N
```

Agents overlap if `arc_spacing < 2 × radius`. **ORCA cannot recover from overlapping starting positions** — it only prevents future collisions, not resolve existing penetrations.

### Formula for minimum safe FORM_RADIUS

```cpp
// No overlap (absolute minimum):
FORM_RADIUS_min = (N * 2 * radius) / (2 * π)

// Comfortable gap (recommended — matches original Circle.cc gap/diameter ratio):
FORM_RADIUS = (N * (2 * radius + gap)) / (2 * π)
// where gap ≈ 0.68 × diameter  →  gap ≈ 1.36 × radius
```

### Example (`N=150, radius=30`)

| FORM_RADIUS | Arc spacing | Gap | Notes |
|---|---|---|---|
| 1433 | 60.0 | 0.0 | Absolute minimum — agents touching |
| 1700 | 71.2 | 11.2 | Too small — 18% gap, agents almost touching |
| **2400** | **100.5** | **40.5** | ✓ Matches original Circle.cc ratio (~68% gap) |
| 3000 | 125.7 | 65.7 | Spacious — good for high-speed scenarios |

Use `FORM_RADIUS ≈ 2400` for `N=150, radius=30` as the baseline.

### Matching the original Circle.cc spacing ratio

Original: `N=250, radius=1.5, FORM_RADIUS=200`  
→ gap/radius = `(2π×200/250 - 3) / 1.5 ≈ 1.35`

To preserve this ratio for any `N` and `radius`:

```cpp
float gap = 1.35f * radius;
float FORM_RADIUS = (N * (2.0f * radius + gap)) / (2.0f * M_PI);
```

---

## raylib: Drawing Texture Centered with Rotation

### Wrong — rotates from top-left corner

```cpp
DrawTextureEx(texture, { pos.x() - w/2, pos.y() - h/2 }, rotation, scale, color);
```

### Correct — rotates around texture center

```cpp
DrawTexturePro(
    texture,
    Rectangle{ 0.0f, 0.0f, (float)texture.width, (float)texture.height },  // source
    Rectangle{ pos.x(), pos.y(), (float)texture.width, (float)texture.height },  // dest
    Vector2{ texture.width / 2.0f, texture.height / 2.0f },  // origin = pivot = center
    rotation,
    color
);
```

`DrawTexturePro`'s `origin` is the pivot point for **both rotation and positioning** — setting it to `{w/2, h/2}` means the center of the texture is placed at `pos` and rotates around `pos`.
