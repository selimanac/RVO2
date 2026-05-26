#include "RVO.h"
#include "raylib.h"

static const int   SCREEN_W = 1920;
static const int   SCREEN_H = 1280;

static const int   NUM_AGENTS = 1500;
static const float RVO_TWO_PI = 6.28318530717958647692f;
static const float FORM_RADIUS = 2400.0f; // world units — gap ~40 units per agent (matches original Circle.cc spacing ratio)
// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Simulation setup
// ---------------------------------------------------------------------------
static void setupScenario(RVO::RVOSimulator* sim, std::vector<RVO::Vector2>& goals)
{
    goals.clear();
    sim->setTimeStep(0.55f);
    sim->setAgentDefaults(380.0f, 5U, 20.0f, 1.0f, 30.0f, 20.0f); // You don't — with no obstacles it has zero effect on behavior. But the code computes 1.0F / timeHorizonObst_ unconditionally, so passing 0 produces +Infinity. Set any positive number (e.g. 1.0f) and it's safely ignored.

    for (std::size_t i = 0; i < (std::size_t)NUM_AGENTS; ++i)
    {
        float angle = static_cast<float>(i) * RVO_TWO_PI * (1.0f / NUM_AGENTS);
        sim->addAgent(FORM_RADIUS * RVO::Vector2(std::cos(angle), std::sin(angle)));
        goals.push_back(-sim->getAgentPosition(i));
    }
}

static void setPreferredVelocities(RVO::RVOSimulator* sim, const std::vector<RVO::Vector2>& goals)
{
    for (int i = 0; i < NUM_AGENTS; ++i)
    {
        RVO::Vector2 goalVec = goals[i] - sim->getAgentPosition(i);
        if (RVO::absSq(goalVec) > 1.0f)
            goalVec = RVO::normalize(goalVec) * sim->getAgentMaxSpeed(i);
        sim->setAgentPrefVelocity(i, goalVec);
    }
}

static bool reachedGoal(RVO::RVOSimulator*               sim,
                        const std::vector<RVO::Vector2>& goals)
{
    for (std::size_t i = 0; i < (std::size_t)NUM_AGENTS; ++i)
    {
        float r = sim->getAgentRadius(i);
        if (RVO::absSq(sim->getAgentPosition(i) - goals[i]) > r * r)
        {
            return false;
        }
        else
        {
            printf("SET FUCK\n");
            sim->setAgentVelocity(i, RVO::Vector2());
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------
static inline Color lerpColor(Color a, Color b, float t)
{
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    return {
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        (unsigned char)(a.a + (b.a - a.a) * t)
    };
}

int main()
{
    // Init raylib
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "FABRIK-C");

    // Camera
    Camera2D camera = { { SCREEN_W / 2.0f, SCREEN_H / 2.0f }, { 0.0f, 0.0f }, 0.0f, 0.2f };

    // Textures
    Texture2D agent_texture = LoadTexture("bin/resources/agent.png");

    // Colors
    Color agentFar = { 220, 50, 30, 255 };
    Color agentNear = { 0, 220, 80, 255 };
    Color hudColor = { 200, 200, 210, 255 };

    // Init SIM
    RVO::RVOSimulator*        sim = new RVO::RVOSimulator();
    std::vector<RVO::Vector2> goals;
    setupScenario(sim, goals);
    std::vector<float> agentRotations(NUM_AGENTS, 0.0f); // last facing angle per agent
    bool               goalReached = false;
    double             simMs = 0.0;
    double             drawMs = 0.0;
    int                stepCount = 0;
    int                goalCheckCtr = 0; // Fix E: lazy goal check

    // LOOP
    while (!WindowShouldClose())
    {
        // --- Input ---
        float scroll = GetMouseWheelMove();
        if (scroll != 0.0f)
        {
            camera.zoom += scroll * 0.1f;
            if (camera.zoom < 0.2f)
                camera.zoom = 0.2f;
            if (camera.zoom > 5.0f)
                camera.zoom = 5.0f;
        }

        // SIM UPDATE
        double t0 = GetTime();
        if (!goalReached)
        {
            setPreferredVelocities(sim, goals);
            sim->doStep();
            ++stepCount;
            ++goalCheckCtr;

            if (goalCheckCtr >= 30)
            {
                goalCheckCtr = 0;

                if (reachedGoal(sim, goals))
                {
                    goalReached = true;
                }
            }
        }
        simMs = (GetTime() - t0) * 1000.0;

        // DRAW
        double t1 = GetTime();
        BeginDrawing();
        ClearBackground(Color { 30, 30, 36, 255 });

        BeginMode2D(camera);

        for (int i = 0; i < NUM_AGENTS; ++i)
        {
            RVO::Vector2 pos = sim->getAgentPosition(i);
            RVO::Vector2 vel = sim->getAgentVelocity(i);
            float        dist = RVO::abs(goals[i] - pos);
            float        progress = 1.0f - dist / (2.0f * FORM_RADIUS);
            Color        col = lerpColor(agentFar, agentNear, progress);

            if (RVO::abs(vel) > 0.1f)
                agentRotations[i] = atan2f(vel.y(), vel.x()) * RAD2DEG;

            // DrawTexture(agent_texture, pos.x(), pos.y(), col);
            DrawTexturePro(
            agent_texture,
            Rectangle { 0.0f, 0.0f, (float)agent_texture.width, (float)agent_texture.height },
            Rectangle { pos.x(), pos.y(), (float)agent_texture.width, (float)agent_texture.height },
            Vector2 { agent_texture.width / 2.0f, agent_texture.height / 2.0f },
            agentRotations[i],
            col);
        }

        EndMode2D();

        char buf[96];
        snprintf(buf, sizeof(buf), "T: %.2f\nSteps: %d\nAgents: %d\nsim: %.2fms\ndraw: %.2fms", sim->getGlobalTime(), stepCount, NUM_AGENTS, simMs, drawMs);
        DrawText(buf, 10, 40, 18, hudColor);

        DrawFPS(10, 10);
        EndDrawing();
        drawMs = (GetTime() - t1) * 1000.0;
    }

    CloseWindow();
    return 0;
}
