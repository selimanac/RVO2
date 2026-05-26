/*
 * raylib_follow.cpp
 * RVO2 Library — Raylib Visualization
 *
 * Interactive example: keyboard-controlled player + ORCA enemy agents.
 *
 * Controls:
 *   Arrow keys  — move player (8 directions)
 *   SPACE       — spawn 10 enemies at origin
 *   F           — toggle enemy follow (on/off)
 *   Scroll      — zoom
 *   ESC         — quit
 */

#include "RVO.h"
#include "raylib.h"

#include <cmath>
#include <cstddef>
#include <vector>
#include <random>
// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const int   SCREEN_W = 1920;
static const int   SCREEN_H = 1280;
static const float PLAYER_RADIUS = 40.0f;
static const float ENEMY_RADIUS = 20.0f;
static const float PLAYER_SPEED = 750.0f; // world units/sec
static const float ENEMY_SPEED = 200.0f;  // world units/sec
static const int   ENEMIES_PER_SPAWN = 100;

struct Unit
{
    std::size_t  simIdx;
    bool         selected;
    float        rotation;
    RVO::Vector2 goal;
    bool         hasGoal;
    float        unit_radius;
    float        unit_speed;
};
static std::vector<Unit> units;

// ---------------------------------------------------------------------------
// Simulation state
// ---------------------------------------------------------------------------
static RVO::RVOSimulator* sim = 0;
static std::size_t        playerIdx = 0;
static RVO::Vector2       playerPos(0.0f, 0.0f);
static float              playerRotation = 0.0f;
static std::size_t        enemyCount = 0;
static std::vector<float> enemyRotations;
static bool               followPlayer = false;

const float               unit_sizes[4] = { 20, 30, 40, 60 };
const float               unit_speeds[4] = { 700, 600, 500, 400 };
std::random_device        rd;
std::mt19937              gen(rd());

// ---------------------------------------------------------------------------
// Setup — adds only the player agent
// ---------------------------------------------------------------------------
static void setupSim()
{
    sim->setAgentDefaults(
    ENEMY_SPEED * 3.0f + 2.0f * PLAYER_RADIUS, // neighborDist (formula)
    5U,                                        // maxNeighbors
    3.0f,                                      // timeHorizon
    1.0f,                                      // timeHorizonObst
    PLAYER_RADIUS,                             // radius
    PLAYER_SPEED * 2.0f                        // maxSpeed (high — we override position)
    );
    playerIdx = sim->addAgent(playerPos);
}

// ---------------------------------------------------------------------------
// Spawn enemies in a small circle — non-overlapping so ORCA can compute
// avoidance directions immediately. All at (0,0) → degenerate ORCA.
// ---------------------------------------------------------------------------
static void spawnEnemies(int count)
{
    // Minimum circle radius for non-overlapping placement

    sim->setAgentDefaults(
    ENEMY_SPEED * 3.0f + 2.0f * ENEMY_RADIUS, // neighborDist = 340
    5U,
    3.0f,
    1.0f,
    ENEMY_RADIUS,
    ENEMY_SPEED);

    for (int i = 0; i < count; ++i)
    {
        std::uniform_int_distribution<int> dist(0, 3);
        uint8_t                            random_num = static_cast<uint8_t>(dist(gen));
        float                              unit_radius = unit_sizes[random_num];
        float                              unit_speed = unit_speeds[random_num];

        float                              spawnRadius = (count * 1.2f * unit_radius) / (2.0f * 3.14159265f);
        if (spawnRadius < unit_radius * 1.2f)
            spawnRadius = unit_radius * 1.2f;

        float       angle = (2.0f * 3.14159265f) * static_cast<float>(i) / static_cast<float>(count);

        std::size_t idx = sim->addAgent(RVO::Vector2(cosf(angle) * spawnRadius, sinf(angle) * spawnRadius), unit_speed * 3.0f + 2.0f * unit_radius, 30U, 10.f, 1.0f, unit_radius, unit_speed);

        Unit        u;
        u.simIdx = idx;
        u.rotation = 0.0f;
        u.unit_radius = unit_radius;
        u.unit_speed = unit_speed;
        units.push_back(u);

        enemyRotations.push_back(0.0f);
        ++enemyCount;
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "RVO2 — Follow");

    // Camera — follows player
    Camera2D camera = { { SCREEN_W / 2.0f, SCREEN_H / 2.0f }, { 0.0f, 0.0f }, 0.0f, 0.5f };

    // Textures
    Texture2D enemy_texture = LoadTexture("bin/resources/agent.png");
    Texture2D player_texture = LoadTexture("bin/resources/player.png");

    // Sim
    sim = new RVO::RVOSimulator();
    setupSim();

    double simMs = 0.0;
    double drawMs = 0.0;

    // LOOP
    while (!WindowShouldClose())
    {
        // Clamp dt: guard against first-frame zero and low-fps instability.
        // maxSpeed * dt / radius must stay well below 1 for stable ORCA.
        // At ENEMY_SPEED=200, ENEMY_RADIUS=20: limit is dt < 0.1 (10fps).
        // Clamping to 0.05 (20fps) keeps the ratio at 0.5 — a safe margin.
        float dt = GetFrameTime();
        if (dt <= 0.0f || dt > 0.05f)
            dt = 0.016f;
        sim->setTimeStep(dt);

        // ---- Input -------------------------------------------------------

        float scroll = GetMouseWheelMove();
        if (scroll != 0.0f)
        {
            camera.zoom += scroll * 0.1f;
            if (camera.zoom < 0.1f)
                camera.zoom = 0.1f;
            if (camera.zoom > 5.0f)
                camera.zoom = 5.0f;
        }

        if (IsKeyPressed(KEY_SPACE))
            spawnEnemies(ENEMIES_PER_SPAWN);

        if (IsKeyPressed(KEY_F))
            followPlayer = !followPlayer;

        // ---- Player 8-direction movement ---------------------------------

        RVO::Vector2 moveDir(0.0f, 0.0f);
        if (IsKeyDown(KEY_RIGHT))
            moveDir = moveDir + RVO::Vector2(1.0f, 0.0f);
        if (IsKeyDown(KEY_LEFT))
            moveDir = moveDir + RVO::Vector2(-1.0f, 0.0f);
        if (IsKeyDown(KEY_DOWN))
            moveDir = moveDir + RVO::Vector2(0.0f, 1.0f); // raylib Y: down = +
        if (IsKeyDown(KEY_UP))
            moveDir = moveDir + RVO::Vector2(0.0f, -1.0f);

        bool isMoving = RVO::absSq(moveDir) > 0.0f;
        if (isMoving)
        {
            moveDir = RVO::normalize(moveDir);
            playerRotation = atan2f(moveDir.y(), moveDir.x()) * RAD2DEG;
        }

        RVO::Vector2 newPlayerPos = playerPos + moveDir * (PLAYER_SPEED * dt);

        // ---- Sim step ----------------------------------------------------

        double t0 = GetTime();

        // Player pref velocity = zero (position is overridden after doStep)
        sim->setAgentPrefVelocity(playerIdx, RVO::Vector2(0.0f, 0.0f));

        // Enemy preferred velocities
        std::size_t totalAgents = sim->getNumAgents();
        for (std::size_t i = 1; i < totalAgents; ++i)
        {
            RVO::Vector2 enemyPos = sim->getAgentPosition(i);
            RVO::Vector2 toPlayer = playerPos - enemyPos;
            float        distSq = RVO::absSq(toPlayer);

            if (followPlayer)
            {
                // Arrival behaviour: decelerate smoothly into a stop ring at
                // physical contact distance (player_r + enemy_r).  This lets
                // ORCA distribute enemies around the perimeter naturally instead
                // of all crowding toward the exact same point at full speed.
                float stopDist = PLAYER_RADIUS + units[i - 1].unit_radius * 2; // ~60 units — touch the player
                float slowDist = stopDist + units[i - 1].unit_radius * 5.0f;   // ~160 units — start braking
                float dist = sqrtf(distSq);

                if (dist <= stopDist)
                {
                    sim->setAgentPrefVelocity(i, RVO::Vector2(0.0f, 0.0f));
                }
                else if (dist < slowDist)
                {
                    float t = (dist - stopDist) / (slowDist - stopDist); // 0..1
                    sim->setAgentPrefVelocity(i, RVO::normalize(toPlayer) * units[i - 1].unit_speed * t);
                }
                else
                {
                    sim->setAgentPrefVelocity(i, RVO::normalize(toPlayer) * units[i - 1].unit_speed);
                }
            }
            else
            {
                // When idle: drift away from player if within 5× radius.
                // Gives ORCA a non-zero velocity to work with so separation
                // and player-push effects can be computed.
                float repelRange = units[i].unit_radius * 100.0f;
                if (distSq < repelRange * repelRange && distSq > 0.0001f)
                    sim->setAgentPrefVelocity(i, RVO::normalize(enemyPos - playerPos) * (units[i].unit_speed * 0.3f));
                else
                    sim->setAgentPrefVelocity(i, RVO::Vector2(0.0f, 0.0f));
            }
        }

        sim->doStep();

        // Hard-stop: force-zero velocity for enemies that reached the arrival
        // zone.  prefVel=0 alone is not enough — ORCA still produces non-zero
        // velocities when surrounding enemies push inward.  Overriding after
        // doStep() guarantees they stay put.
        if (followPlayer)
        {
            float stopDist = PLAYER_RADIUS + ENEMY_RADIUS * 2.0f;
            float stopDistSq = stopDist * stopDist;
            for (std::size_t i = 1; i < totalAgents; ++i)
            {
                RVO::Vector2 toP = playerPos - sim->getAgentPosition(i);
                if (RVO::absSq(toP) <= stopDistSq)
                    sim->setAgentVelocity(i, RVO::Vector2(0.0f, 0.0f));
            }
        }

        // Override player position and velocity — bypasses ORCA for player
        playerPos = newPlayerPos;
        sim->setAgentPosition(playerIdx, playerPos);
        sim->setAgentVelocity(playerIdx, isMoving ? moveDir * PLAYER_SPEED : RVO::Vector2(0.0f, 0.0f));

        // Camera tracks player
        camera.target = { playerPos.x(), playerPos.y() };

        simMs = (GetTime() - t0) * 1000.0;

        // ---- Draw --------------------------------------------------------

        double t1 = GetTime();
        BeginDrawing();
        ClearBackground(Color { 30, 30, 36, 255 });

        BeginMode2D(camera);

        // Enemies
        float ew = (float)enemy_texture.width;
        float eh = (float)enemy_texture.height;

        for (std::size_t i = 1; i < totalAgents; ++i)
        {
            RVO::Vector2 pos = sim->getAgentPosition(i);
            RVO::Vector2 vel = sim->getAgentVelocity(i);

            if (RVO::abs(vel) > 0.1f)
                enemyRotations[i - 1] = atan2f(vel.y(), vel.x()) * RAD2DEG;

            float escale = (units[i].unit_radius * 2.0f) / ew; // scale texture to match diameter
            float dw = ew * escale;
            float dh = eh * escale;
            DrawTexturePro(
            enemy_texture,
            Rectangle { 0.0f, 0.0f, ew, eh },
            Rectangle { pos.x(), pos.y(), dw, dh },
            Vector2 { dw / 2.0f, dh / 2.0f },
            enemyRotations[i - 1],
            WHITE);
        }

        // Player
        float pw = (float)player_texture.width;
        float ph = (float)player_texture.height;
        float pscale = (PLAYER_RADIUS * 2.0f) / pw;
        float pdw = pw * pscale;
        float pdh = ph * pscale;
        DrawTexturePro(
        player_texture,
        Rectangle { 0.0f, 0.0f, pw, ph },
        Rectangle { playerPos.x(), playerPos.y(), pdw, pdh },
        Vector2 { pdw / 2.0f, pdh / 2.0f },
        playerRotation,
        WHITE);

        EndMode2D();

        // HUD
        char buf[128];
        snprintf(buf, sizeof(buf), "Enemies: %zu\nFollow: %s\nSim: %.2fms\nDraw: %.2fms\n[SPACE] spawn  [F] follow  [Arrows] move", enemyCount, followPlayer ? "ON" : "OFF", simMs, drawMs);
        DrawFPS(10, 10);
        DrawText(buf, 10, 40, 18, Color { 200, 200, 210, 255 });

        EndDrawing();
        drawMs = (GetTime() - t1) * 1000.0;
    }

    delete sim;
    CloseWindow();
    return 0;
}
