
#include "RVO.h"
#include "raylib.h"
#include <random>

static const int          SCREEN_W = 1920;
static const int          SCREEN_H = 1280;

static const float        RVO_TWO_PI = 6.28318530717958647692f;
static RVO::RVOSimulator* sim = 0;
Texture2D                 agent_texture;
std::vector<RVO::Vector2> arena;

struct Unit
{
    std::size_t  simIdx;
    float        rotation;
    RVO::Vector2 goal;
    float        unit_radius;
    float        unit_speed;
};
static std::vector<Unit> enemies;
static std::size_t       enemyCount = 0;

const float              unit_sizes[4] = { 30, 30, 30, 30 };
const float              unit_speeds[4] = { 10, 10, 10, 10 };
std::size_t              max_neighbors[4] = { 5, 5, 5, 5 };
const RVO::Vector2       goals[1] = { { 4317.0f, 1269.0f } };
const RVO::Vector2       spawn_points[3] = { { 489.0f, 495.0f }, { 945.f, 4400.f }, { 3158.f, 3925.f } };

std::random_device       rd;
std::mt19937             gen(rd());

void                     DrawArenaBounds()
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

static void spawnEnemies(int count)
{
    for (int i = 0; i < count; ++i)
    {
        std::uniform_int_distribution<int>    dist(0, 2);
        uint8_t                               enemy_type = static_cast<uint8_t>(dist(gen));

        std::uniform_int_distribution<int>    spawn_dist(0, 2);
        const int                             spawn_point = spawn_dist(gen);

        float                                 unit_radius = unit_sizes[0];
        float                                 unit_speed = unit_speeds[0];
        std::size_t                           max_neighbor = max_neighbors[0];
        std::uniform_real_distribution<float> spawn_offset_dist(-unit_radius * 0.5f, unit_radius * 0.5f);
        const RVO::Vector2                    spawn_offset(spawn_offset_dist(gen), spawn_offset_dist(gen));
        const RVO::Vector2                    spawn = spawn_points[spawn_point] + spawn_offset;

        std::size_t                           idx = sim->addAgent(spawn, 150, static_cast<size_t>(max_neighbor), 5.0f, 10.0f, unit_radius, unit_speed);

        printf("spawn[%d] X: %f - Y: %f\n",
               spawn_point,
               spawn.x(),
               spawn.y());

        std::uniform_int_distribution<int> goal_y_dist(1250, 2360);
        float                              goal_y = static_cast<float>(goal_y_dist(gen));

        Unit                               u;
        u.simIdx = idx;
        u.rotation = 0.0f;
        u.unit_radius = unit_radius;
        u.unit_speed = unit_speed;
        //   printf("goal_y %f\n", goal_y);
        u.goal = { goals[0].x(), goal_y };
        enemies.push_back(u);

        ++enemyCount;
    }
}

static void setupScenario()
{
    sim->setTimeStep(0.55f);

    // --- Generated RVO2 Boundary Vertices (Raylib Y-Down) ---

    arena.push_back(RVO::Vector2(0.0f, 0.0f));
    arena.push_back(RVO::Vector2(2308.0f, 0.0f));
    arena.push_back(RVO::Vector2(2308.0f, 1107.0f));
    arena.push_back(RVO::Vector2(4682.0f, 1107.0f));
    arena.push_back(RVO::Vector2(4682.0f, 2517.0f));
    arena.push_back(RVO::Vector2(3211.0f, 2517.0f));
    arena.push_back(RVO::Vector2(3211.0f, 3208.0f));
    arena.push_back(RVO::Vector2(3955.0f, 3208.0f));
    arena.push_back(RVO::Vector2(3955.0f, 4470.0f));
    arena.push_back(RVO::Vector2(2370.0f, 4470.0f));
    arena.push_back(RVO::Vector2(2370.0f, 3228.0f));
    arena.push_back(RVO::Vector2(1963.0f, 3228.0f));
    arena.push_back(RVO::Vector2(1963.0f, 2070.0f));
    arena.push_back(RVO::Vector2(1457.0f, 2070.0f));
    arena.push_back(RVO::Vector2(1457.0f, 3474.0f));
    arena.push_back(RVO::Vector2(2135.0f, 3474.0f));
    arena.push_back(RVO::Vector2(2135.0f, 4562.0f));
    arena.push_back(RVO::Vector2(3.0f, 4562.0f));
    arena.push_back(RVO::Vector2(3.0f, 1430.0f));
    arena.push_back(RVO::Vector2(1412.0f, 1430.0f));
    arena.push_back(RVO::Vector2(1412.0f, 1037.0f));
    arena.push_back(RVO::Vector2(0.0f, 1037.0f));

    sim->addObstacle(arena);
    sim->processObstacles();
}

static void setPreferredVelocities()
{
    for (int i = 0; i < enemyCount; ++i)
    {
        // 1. Calculate your standard direct velocity to the goal
        RVO::Vector2 goalDirection = enemies[i].goal - sim->getAgentPosition(enemies[i].simIdx);
        RVO::Vector2 prefVelocity = normalize(goalDirection) * sim->getAgentMaxSpeed(enemies[i].simIdx);

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

        // DrawTexture(agent_texture, pos.x(), pos.y(), col);
        DrawTexturePro(
        agent_texture,
        Rectangle { 0.0f, 0.0f, (float)agent_texture.width, (float)agent_texture.height },
        Rectangle { pos.x(), pos.y(), (float)agent_texture.width, (float)agent_texture.height },
        Vector2 { agent_texture.width / 2.0f, agent_texture.height / 2.0f },
        enemies[i].rotation,
        WHITE);
    }
}

int main()
{
    sim = new RVO::RVOSimulator();
    setupScenario();

    // Init raylib
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "FABRIK-C");

    agent_texture = LoadTexture("bin/resources/agent.png");
    Camera2D camera = { { SCREEN_W / 2.0f, SCREEN_H / 2.0f }, { 5000 / 2.0f, 5000 / 2.0f }, 0.0f, 0.5f };

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
            spawnEnemies(10);

        setPreferredVelocities();
        sim->doStep();

        BeginDrawing();
        ClearBackground(Color { 30, 30, 36, 255 });
        BeginMode2D(camera);

        DrawEnemies();
        DrawArenaBounds();

        //   DrawCircleV({ goals[0].x(), goals[0].y() }, 6.0f, RED);
        DrawRectangle(goals[0].x(), 1211, 120, 1200, RED);
        DrawCircleV({ spawn_points[0].x(), spawn_points[0].y() }, 6.0f, RED);
        DrawCircleV({ spawn_points[1].x(), spawn_points[1].y() }, 6.0f, BLUE);
        EndMode2D();
        DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
