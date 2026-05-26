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
#if _OPENMP
#include <omp.h>
#endif /* _OPENMP */
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
const float               unit_speeds[4] = { 450, 400, 340, 280 };
std::size_t               max_neighbors[4] = { 6, 15, 20, 25 };
std::random_device        rd;
std::mt19937              gen(rd());

// ---------------------------------------------------------------------------
// Setup — adds only the player agent
// ---------------------------------------------------------------------------
static void setupSim()
{
    sim->setAgentDefaults(
    PLAYER_RADIUS * 8.0f + 150.0f, // neighborDist
    5U,                            // maxNeighbors
    3.0f,                          // timeHorizon
    1.0f,                          // timeHorizonObst
    PLAYER_RADIUS,                 // radius
    PLAYER_SPEED * 2.0f            // maxSpeed (high — we override position)
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
    ENEMY_RADIUS * 8.0f + 150.0f, // neighborDist
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

        std::size_t max_neighbor = max_neighbors[random_num];

        std::size_t idx = sim->addAgent(RVO::Vector2(cosf(angle) * spawnRadius, sinf(angle) * spawnRadius), unit_radius * 8.0f + 150.0f, static_cast<size_t>(max_neighbor), 3.0f, 1.0f, unit_radius, unit_speed);

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

    double      simMs = 0.0;
    double      drawMs = 0.0;
    float       avgAgentNeighbors = 0.0f;
    std::size_t maxAgentNeighborsSeen = 0;

    // LOOP
    while (!WindowShouldClose())
    {
        // Use fixed sim timestep for stable ORCA behaviour regardless of frame rate.
        // Player movement still uses the actual frame time.
        float dt = GetFrameTime();
        if (dt <= 0.0f || dt > 0.05f)
            dt = 0.016f;
        sim->setTimeStep(1.0f / 60.0f);

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

        // Update player in sim BEFORE enemies compute ORCA constraints,
        // so neighbours avoid the current player state not last frame's.
        playerPos = newPlayerPos;
        RVO::Vector2 playerVel = isMoving ? moveDir * PLAYER_SPEED : RVO::Vector2(0.0f, 0.0f);
        sim->setAgentPosition(playerIdx, playerPos);
        sim->setAgentVelocity(playerIdx, playerVel);
        sim->setAgentPrefVelocity(playerIdx, RVO::Vector2(0.0f, 0.0f));

        // Enemy preferred velocities
        std::size_t totalAgents = sim->getNumAgents();
        int         totalAgentsInt = static_cast<int>(totalAgents);
#ifdef _OPENMP
#pragma omp parallel for
#endif /* _OPENMP */
        for (int i = 1; i < totalAgentsInt; ++i)
        {
            RVO::Vector2 enemyPos = sim->getAgentPosition(static_cast<std::size_t>(i));
            RVO::Vector2 toPlayer = playerPos - enemyPos;
            float        distSq = RVO::absSq(toPlayer);

            if (followPlayer)
            {
                // Arrival behaviour: decelerate smoothly into a stop ring at
                // physical contact distance (player_r + enemy_r).
                float stopDist = PLAYER_RADIUS + units[i - 1].unit_radius * 2.0f;
                float slowDist = stopDist + units[i - 1].unit_radius * 5.0f;
                float dist = sqrtf(distSq);

                if (dist <= stopDist)
                {
                    // Inside ring: tiny outward nudge so ORCA can blend crowd
                    // pressure naturally — avoids hard-stop vs ORCA oscillation.
                    if (distSq > 0.0001f)
                        sim->setAgentPrefVelocity(static_cast<std::size_t>(i), RVO::normalize(enemyPos - playerPos) * (units[i - 1].unit_speed * 0.1f));
                    else
                        sim->setAgentPrefVelocity(static_cast<std::size_t>(i), RVO::Vector2(0.0f, 0.0f));
                }
                else if (dist < slowDist)
                {
                    float t = (dist - stopDist) / (slowDist - stopDist); // 0..1
                    sim->setAgentPrefVelocity(static_cast<std::size_t>(i), RVO::normalize(toPlayer) * units[i - 1].unit_speed * t);
                }
                else
                {
                    sim->setAgentPrefVelocity(static_cast<std::size_t>(i), RVO::normalize(toPlayer) * units[i - 1].unit_speed);
                }
            }
            else
            {
                // When idle: drift away from player if within repel range.
                float repelRange = units[i - 1].unit_radius * 100.0f;
                if (distSq < repelRange * repelRange && distSq > 0.0001f)
                    sim->setAgentPrefVelocity(static_cast<std::size_t>(i), RVO::normalize(enemyPos - playerPos) * (units[i - 1].unit_speed * 0.3f));
                else
                    sim->setAgentPrefVelocity(static_cast<std::size_t>(i), RVO::Vector2(0.0f, 0.0f));
            }
        }

        sim->doStep();

        // Re-assert player position/velocity after doStep (ORCA may drift it).
        sim->setAgentPosition(playerIdx, playerPos);
        sim->setAgentVelocity(playerIdx, playerVel);

        std::size_t totalAgentNeighbors = 0;
        maxAgentNeighborsSeen = 0;
        for (std::size_t i = 0; i < totalAgents; ++i)
        {
            std::size_t n = sim->getAgentNumAgentNeighbors(i);
            totalAgentNeighbors += n;
            if (n > maxAgentNeighborsSeen)
                maxAgentNeighborsSeen = n;
        }

        avgAgentNeighbors = totalAgents > 0 ? (float)totalAgentNeighbors / (float)totalAgents : 0.0f;

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

            float escale = (units[i - 1].unit_radius * 2.0f) / ew; // scale texture to match diameter
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
        char buf[256];
#ifdef _OPENMP
        int  ompThreads = omp_get_max_threads();
#else
        int  ompThreads = 1;
#endif /* _OPENMP */
        snprintf(
        buf,
        sizeof(buf),
        "Enemies: %zu\nFollow: %s\nSim: %.2fms\nDraw: %.2fms\nNeighbors avg/max: %.1f/%zu\nThreads: %d\n[SPACE] spawn  [F] follow  [Arrows] move",
        enemyCount,
        followPlayer ? "ON" : "OFF",
        simMs,
        drawMs,
        avgAgentNeighbors,
        maxAgentNeighborsSeen,
        ompThreads);
        DrawFPS(10, 10);
        DrawText(buf, 10, 40, 18, Color { 200, 200, 210, 255 });

        EndDrawing();
        drawMs = (GetTime() - t1) * 1000.0;
    }

    delete sim;
    CloseWindow();
    return 0;
}
