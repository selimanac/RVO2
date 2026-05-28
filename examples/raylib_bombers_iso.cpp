
#include "RVO.h"
#include "raylib.h"
#include <cstdint>
#include <random>

#if _OPENMP
#include <omp.h>
#endif /* _OPENMP */

static const int          SCREEN_W = 1920;
static const int          SCREEN_H = 1280;

static const float        RVO_TWO_PI = 6.28318530717958647692f;
static RVO::RVOSimulator* sim = 0;
Texture2D                 agent_texture;
Texture2D                 obs_texture;
Texture2D                 turrent_1_texture;
Texture2D                 turrent_2_texture;
std::vector<RVO::Vector2> arena;

struct Unit
{
    std::size_t  simIdx;
    float        rotation;
    RVO::Vector2 goal;
    RVO::Vector2 knockback;
    float        knockbackTime;
    float        unit_radius;
    float        unit_speed;
    uint8_t      health;
};
static std::vector<Unit> enemies;
static std::size_t       enemyCount = 0;

const float              unit_sizes[4] = { 15, 30, 30, 30 };
const float              unit_speeds[4] = { 5, 5, 5, 5 };
std::size_t              max_neighbors[4] = { 5, 5, 5, 5 };
const RVO::Vector2       goals[3] = { { 3585.0f, 2272.0f }, { 4072.0f, 2000.0f }, { 4545.0f, 1792.0f } };
const RVO::Vector2       spawn_points[3] = { { 240.0f, 912.0f }, { 1128.f, 463.f }, { 1780.f, 130.f } };

Vector2                  mouseScreen;
Vector2                  mouseWorld;

std::random_device       rd;
std::mt19937             gen(rd());

const float              BOMB_RADIUS = 360.0f;
const float              BOMB_FORCE = 82.0f;
const float              BOMB_DURATION = 20.45f;
const float              TURRENT_FIRE_INTERVAL = 0.2f;
const float              TURRENT_HIT_RADIUS = 55.0f;
const float              TURRENT_KNOCKBACK_FORCE = 55.0f;
const float              TURRENT_KNOCKBACK_DURATION = 3.2f;
const RVO::Vector2       TURRENT_1_POSITION(2232.0f, 900.0f);
const RVO::Vector2       TURRENT_1_TARGET(1992.0f, 732.0f);
const RVO::Vector2       TURRENT_2_POSITION(2124.0f, 2034.0f);
const RVO::Vector2       TURRENT_2_TARGET(1944.0f, 1612.0f);

// STATS
double simMs = 0.0;
double drawMs = 0.0;

void   DrawArenaBounds()
{
    if (arena.empty())
        return;

    float  wallThickness = 5.0f;
    Color  wallColor = MAROON; // A nice solid color for boundaries
    Color  cornerColor = DARKGRAY;

    size_t vertexCount = arena.size();

    for (size_t i = 0; i < vertexCount; ++i)
    {
        // 1. Get current vertex and the next vertex
        // The modulo operator (%) automatically hooks the last vertex back to vertex 0
        RVO::Vector2 rvoStart = arena[i];
        RVO::Vector2 rvoEnd = arena[(i + 1) % vertexCount];

        // 2. Convert RVO::Vector2 (uses .x() and .y()) to Raylib Vector2 (uses .x and .y)
        Vector2 raylibStart = { rvoStart.x(), rvoStart.y() };
        Vector2 raylibEnd = { rvoEnd.x(), rvoEnd.y() };

        // 3. Draw the wall line segment
        DrawLineEx(raylibStart, raylibEnd, wallThickness, wallColor);

        // 4. (Optional) Draw dots on corners to make map debugging easier
        DrawCircleV(raylibStart, 6.0f, cornerColor);
    }
}

static void AddBombImpact()
{
    const RVO::Vector2 center(mouseWorld.x, mouseWorld.y);

    for (Unit& enemy : enemies)
    {
        const RVO::Vector2 pos = sim->getAgentPosition(enemy.simIdx);
        RVO::Vector2       fromBlast = pos - center;
        float              distance = RVO::abs(fromBlast);

        if (distance > BOMB_RADIUS)
            continue;

        if (distance < 0.001f)
        {
            std::uniform_real_distribution<float> angle_dist(0.0f, RVO_TWO_PI);
            const float                           angle = angle_dist(gen);
            fromBlast = RVO::Vector2(std::cos(angle), std::sin(angle));
            distance = 1.0f;
        }

        const float falloff = 1.0f - distance / BOMB_RADIUS;
        const float force = BOMB_FORCE * falloff * falloff;
        enemy.knockback = RVO::normalize(fromBlast) * force;
        enemy.knockbackTime = BOMB_DURATION;
        sim->setAgentVelocity(enemy.simIdx, enemy.knockback);
        sim->setAgentMaxSpeed(enemy.simIdx, std::max(enemy.unit_speed, force));
    }
}

static void ApplyKnockback(Unit& enemy, const RVO::Vector2& direction, float force, float duration)
{
    enemy.knockback = RVO::normalize(direction) * force;
    enemy.knockbackTime = duration;
    sim->setAgentVelocity(enemy.simIdx, enemy.knockback);
    sim->setAgentMaxSpeed(enemy.simIdx, std::max(enemy.unit_speed, force));
}

static void FireTurrent(const RVO::Vector2& turrentPosition, const RVO::Vector2& targetPosition)
{
    const RVO::Vector2 fireDirection = targetPosition - turrentPosition;
    float              bestDistSq = TURRENT_HIT_RADIUS * TURRENT_HIT_RADIUS;
    Unit*              target = NULL;

    for (Unit& enemy : enemies)
    {
        const RVO::Vector2 enemyPos = sim->getAgentPosition(enemy.simIdx);
        const float        distSq = RVO::absSq(enemyPos - targetPosition);

        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            target = &enemy;
        }
    }

    if (target != NULL)
    {
        ApplyKnockback(*target, fireDirection, TURRENT_KNOCKBACK_FORCE, TURRENT_KNOCKBACK_DURATION);
    }
}

static void UpdateTurrents(float dt)
{
    static float turrent1Cooldown = 0.0f;
    static float turrent2Cooldown = 0.0f;

    turrent1Cooldown -= dt;
    turrent2Cooldown -= dt;

    if (turrent1Cooldown <= 0.0f)
    {
        FireTurrent(TURRENT_1_POSITION, TURRENT_1_TARGET);
        turrent1Cooldown = TURRENT_FIRE_INTERVAL;
    }

    if (turrent2Cooldown <= 0.0f)
    {
        FireTurrent(TURRENT_2_POSITION, TURRENT_2_TARGET);
        turrent2Cooldown = TURRENT_FIRE_INTERVAL;
    }
}

static void spawnEnemies(int count)
{
    for (int i = 0; i < count; ++i)
    {
        std::uniform_int_distribution<int>    spawn_dist(0, 2);
        const int                             spawn_point = spawn_dist(gen);

        float                                 unit_radius = unit_sizes[0];
        float                                 unit_speed = unit_speeds[0];
        std::size_t                           max_neighbor = max_neighbors[0];
        std::uniform_real_distribution<float> spawn_offset_dist(-unit_radius * 0.5f, unit_radius * 0.5f);
        const RVO::Vector2                    spawn_offset(spawn_offset_dist(gen), spawn_offset_dist(gen));
        const RVO::Vector2                    spawn = spawn_points[spawn_point] + spawn_offset;

        std::size_t                           idx = sim->addAgent(spawn, 150, static_cast<size_t>(max_neighbor), 15.0f, 35.0f, unit_radius, unit_speed);

        std::uniform_int_distribution<int>    goal_dist(0, 2);
        uint8_t                               goal_id = static_cast<uint8_t>(goal_dist(gen));

        Unit                                  u;
        u.simIdx = idx;
        u.rotation = 0.0f;
        u.knockback = RVO::Vector2();
        u.knockbackTime = 0.0f;
        u.unit_radius = unit_radius;
        u.unit_speed = unit_speed;
        u.health = 100;
        u.goal = { goals[goal_id].x(), goals[goal_id].y() };
        enemies.push_back(u);

        ++enemyCount;
    }
}

static void setupScenario()
{
    sim->setTimeStep(0.45f);

    // --- Generated RVO2 Boundary Vertices (Raylib Y-Down) ---

    arena.push_back(RVO::Vector2(3238.0f, 2576.0f));
    arena.push_back(RVO::Vector2(5152.0f, 1619.0f));
    arena.push_back(RVO::Vector2(1914.0f, 0.0f));
    arena.push_back(RVO::Vector2(0.0f, 957.0f));

    // 4 rectangular obstacle blocks (vertices in counterclockwise order)
    std::vector<RVO::Vector2> obs1;

    RVO::Vector2              obsPos(2380.0f, 997.0f);
    RVO::Vector2              obsOrigin(obs_texture.width / 2.0f, obs_texture.height / 2.0f);

    obs1.push_back(RVO::Vector2(0.0f, 184.0f) + obsPos - obsOrigin);
    obs1.push_back(RVO::Vector2(368.0f, 0.0f) + obsPos - obsOrigin);
    obs1.push_back(RVO::Vector2(792.0f, 212.0f) + obsPos - obsOrigin);
    obs1.push_back(RVO::Vector2(424.0f, 396.0f) + obsPos - obsOrigin);

    sim->addObstacle(obs1);
    sim->addObstacle(arena);

    sim->processObstacles();
}

static void setPreferredVelocities()
{
#ifdef _OPENMP
#pragma omp parallel for
#endif /* _OPENMP */
    for (int i = 0; i < enemyCount; ++i)
    {
        // 1. Calculate your standard direct velocity to the goal
        RVO::Vector2 goalDirection = enemies[i].goal - sim->getAgentPosition(enemies[i].simIdx);
        RVO::Vector2 prefVelocity = normalize(goalDirection) * enemies[i].unit_speed;

        if (enemies[i].knockbackTime > 0.0f)
        {
            prefVelocity += enemies[i].knockback;
            enemies[i].knockback *= 0.85f;
            enemies[i].knockbackTime -= 0.30f;

            const float impactSpeed = RVO::abs(prefVelocity);
            sim->setAgentMaxSpeed(enemies[i].simIdx, std::max(enemies[i].unit_speed, impactSpeed));
        }
        else
        {
            enemies[i].knockback = RVO::Vector2();
            sim->setAgentMaxSpeed(enemies[i].simIdx, enemies[i].unit_speed);
        }

        // 2. THE TRICK: Check if RVO2 is slowing the agent down
        RVO::Vector2 currentVelocity = sim->getAgentVelocity(enemies[i].simIdx);
        float        currentSpeed = RVO::abs(currentVelocity);
        const float  maxSpeed = sim->getAgentMaxSpeed(enemies[i].simIdx);
        if (currentSpeed < maxSpeed * 0.85f)
        {
            // Agent is slowing down because of a wall.
            // Create a perpendicular vector (rotate 90 degrees) to break symmetry
            RVO::Vector2 nudge(-prefVelocity.y(), prefVelocity.x());

            // Mix the nudge into the preferred velocity and re-normalize to maxSpeed
            prefVelocity = normalize(prefVelocity + nudge * 0.5f) * sim->getAgentMaxSpeed(enemies[i].simIdx);
        }

        // 3. Pass it back to the simulator
        sim->setAgentPrefVelocity(enemies[i].simIdx, prefVelocity);
    }
}

static void DrawEnemies()
{
    for (int i = 0; i < enemyCount; ++i)
    {
        RVO::Vector2 pos = sim->getAgentPosition(enemies[i].simIdx);
        RVO::Vector2 vel = sim->getAgentVelocity(enemies[i].simIdx);

        enemies[i].rotation = atan2f(vel.y(), vel.x()) * RAD2DEG;

        DrawTexturePro(
        agent_texture,
        Rectangle { 0.0f, 0.0f, (float)agent_texture.width, (float)agent_texture.height },
        Rectangle { pos.x(), pos.y(), (float)agent_texture.width, (float)agent_texture.height },
        Vector2 { agent_texture.width / 2.0f, agent_texture.height / 2.0f },
        enemies[i].rotation,
        WHITE);
    }
}

static void DrawPoints()
{
    // Line
    DrawLineEx({ 3120, 2416 }, { 4840, 1548 }, 15, BLUE);

    for (int i = 0; i < 3; ++i)
    {
        DrawCircleV({ spawn_points[i].x(), spawn_points[i].y() }, 6.0f, GREEN);
    }

    for (int i = 0; i < 3; ++i)
    {
        DrawCircleV({ goals[i].x(), goals[i].y() }, 6.0f, GREEN);
    }
}

static void DrawObs()
{
    DrawTexturePro(
    obs_texture,
    Rectangle { 0.0f, 0.0f, (float)obs_texture.width, (float)obs_texture.height },
    Rectangle { 2380, 997, (float)obs_texture.width, (float)obs_texture.height },
    Vector2 { obs_texture.width / 2.0f, obs_texture.height / 2.0f },
    0.f,
    WHITE);
}
static void DrawTurrents()
{
    DrawTexturePro(
    turrent_2_texture,
    Rectangle { 0.0f, 0.0f, (float)turrent_2_texture.width, (float)turrent_2_texture.height },
    Rectangle { 2155, 2090, (float)turrent_2_texture.width, (float)turrent_2_texture.height },
    Vector2 { turrent_2_texture.width / 2.0f, turrent_2_texture.height / 2.0f },
    0.f,
    WHITE);

    DrawLineEx({ TURRENT_2_POSITION.x(), TURRENT_2_POSITION.y() }, { TURRENT_2_TARGET.x(), TURRENT_2_TARGET.y() }, 1, GREEN);

    DrawTexturePro(
    turrent_1_texture,
    Rectangle { 0.0f, 0.0f, (float)turrent_1_texture.width, (float)turrent_1_texture.height },
    Rectangle { 2242, 926, (float)turrent_1_texture.width, (float)turrent_1_texture.height },
    Vector2 { turrent_2_texture.width / 2.0f, turrent_1_texture.height / 2.0f },
    0.f,
    WHITE);

    DrawLineEx({ TURRENT_1_POSITION.x(), TURRENT_1_POSITION.y() }, { TURRENT_1_TARGET.x(), TURRENT_1_TARGET.y() }, 1, GREEN);
}

static void DrawStats()
{
    // HUD
    char buf[256];
#ifdef _OPENMP
    int ompThreads = omp_get_max_threads();
#else
    int ompThreads = 1;
#endif /* _OPENMP */
    snprintf(
    buf,
    sizeof(buf),
    "Enemies: %zu\nSim: %.2fms\nDraw: %.2fms\nThreads: %d\n[SPACE] spawn",
    enemyCount,
    simMs,
    drawMs,
    ompThreads);
    DrawFPS(10, 10);
    DrawText(buf, 10, 40, 18, Color { 200, 200, 210, 255 });
}

int main()
{
    sim = new RVO::RVOSimulator();

    // Init raylib
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);
    InitWindow(SCREEN_W, SCREEN_H, "FABRIK-C");

    agent_texture = LoadTexture("bin/resources/agent_30.png");
    obs_texture = LoadTexture("bin/resources/obs1.png");

    turrent_1_texture = LoadTexture("bin/resources/turrent1.png");
    turrent_2_texture = LoadTexture("bin/resources/turrent2.png");
    setupScenario();

    Camera2D camera = { { SCREEN_W / 2.0f, SCREEN_H / 2.0f }, { 5198 / 2.0f, 2612 / 2.0f }, 0.0f, 0.5f };

    // LOOP
    while (!WindowShouldClose())
    {
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
            spawnEnemies(100);

        mouseScreen = GetMousePosition();
        mouseWorld = GetScreenToWorld2D(mouseScreen, camera);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            AddBombImpact();
        }

        // SIM STEP
        double t0 = GetTime();
        UpdateTurrents(GetFrameTime());
        setPreferredVelocities();
        sim->doStep();
        simMs = (GetTime() - t0) * 1000.0;

        // DRAW
        double t1 = GetTime();
        BeginDrawing();
        ClearBackground(Color { 30, 30, 36, 255 });
        BeginMode2D(camera);

        DrawEnemies();
        DrawArenaBounds();
        DrawPoints();
        DrawObs();
        DrawTurrents();

        EndMode2D();
        DrawStats();
        EndDrawing();

        drawMs = (GetTime() - t1) * 1000.0;
    }

    CloseWindow();
    return 0;
}
