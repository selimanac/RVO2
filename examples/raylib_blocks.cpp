/*
 * raylib_blocks.cpp
 * RVO2 Library — Raylib Visualization
 *
 * Real-time visualization of the Blocks example:
 * 100 agents in four corner groups navigate through narrow obstacle passages.
 * Agents are colored by group; obstacles and goals are drawn in a cached
 * static texture layer that is rebuilt only on reset.
 *
 * Controls:
 *   R      — reset simulation
 *   V      — toggle velocity lines
 *   ESC    — quit
 *   Scroll — zoom
 *
 * Rendering notes:
 *   - Static layers (obstacles + goal markers) are cached in a RenderTexture2D.
 *   - Agent velocity lines and circles are drawn in separate passes to avoid
 *     per-agent GPU batch flushes from GL_LINES <-> GL_TRIANGLES alternation.
 */

#include "RVO.h"
#include "raylib.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <vector>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const int   SCREEN_W = 1380;
static const int   SCREEN_H = 1380;

static const float RVO_TWO_PI = 6.28318530717958647692f;
static const int   NUM_AGENTS = 200;
static const int   RESET_DELAY = 120;    // frames to wait after goal reached
static const float ENEMY_SPEED = 200.0f; // world units/sec
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

// ---------------------------------------------------------------------------
// Per-group colors  (index = agentIndex % 4)
//   0 = NE → SW   1 = NW → SE   2 = SE → NW   3 = SW → NE
// ---------------------------------------------------------------------------
static const Color GROUP_FAR[4] = {
    { 220, 80, 60, 255 },  // NE : red-orange
    { 60, 120, 220, 255 }, // NW : blue
    { 200, 60, 220, 255 }, // SE : purple
    { 220, 180, 40, 255 }, // SW : amber
};

static const Color GROUP_NEAR[4] = {
    { 60, 220, 80, 255 },  // NE near : green
    { 220, 200, 60, 255 }, // NW near : yellow
    { 60, 220, 200, 255 }, // SE near : teal
    { 80, 80, 220, 255 },  // SW near : indigo
};

// Goal positions match group order above
static const RVO::Vector2 GROUP_GOALS[4] = {
    RVO::Vector2(-1200.0f, -1200.0f),
    RVO::Vector2(1200.0f, -1200.0f),
    RVO::Vector2(-1200.0f, 1200.0f),
    RVO::Vector2(1200.0f, 1200.0f),
};

static const RVO::Vector2 OBS_TL[4] = {
    RVO::Vector2(-250.0f, 122.0f), // obstacle 1 (NW block)
    RVO::Vector2(122.0f, 122.0f),  // obstacle 2 (NE block)
    RVO::Vector2(122.0f, -250.0f), // obstacle 3 (SE block)
    RVO::Vector2(-250.0f, -250.0f) // obstacle 4 (SW block)
};
float obstW = 128.0f;
float obstH = 128.0f;

// ---------------------------------------------------------------------------
// Simulation setup
// ---------------------------------------------------------------------------
static void setupScenario(RVO::RVOSimulator*         sim,
                          std::vector<RVO::Vector2>& goals,
                          std::vector<float>&        initialDists)
{
    std::srand(static_cast<unsigned int>(std::time(NULL)));
    goals.clear();
    initialDists.clear();

    sim->setTimeStep(0.55f);
    sim->setAgentDefaults(1350.0f, 10U, 15.0f, 5.0f, 30.0f, 10.0f);

    for (std::size_t i = 0U; i < 5U; ++i)
    {
        for (std::size_t j = 0U; j < 10U; ++j)
        {
            float        fi = static_cast<float>(i);
            float        fj = static_cast<float>(j);

            RVO::Vector2 p0(855.0f + fi * 60.0f, 855.0f + fj * 60.0f);
            sim->addAgent(p0);
            goals.push_back(GROUP_GOALS[0]);
            initialDists.push_back(RVO::abs(GROUP_GOALS[0] - p0));

            RVO::Vector2 p1(-855.0f - fi * 60.0f, 855.0f + fj * 60.0f);
            sim->addAgent(p1);
            goals.push_back(GROUP_GOALS[1]);
            initialDists.push_back(RVO::abs(GROUP_GOALS[1] - p1));

            RVO::Vector2 p2(855.0f + fi * 60.0f, -855.0f - fj * 60.0f);
            sim->addAgent(p2);
            goals.push_back(GROUP_GOALS[2]);
            initialDists.push_back(RVO::abs(GROUP_GOALS[2] - p2));

            RVO::Vector2 p3(-855.0f - fi * 60.0f, -855.0f - fj * 60.0f);
            sim->addAgent(p3);
            goals.push_back(GROUP_GOALS[3]);
            initialDists.push_back(RVO::abs(GROUP_GOALS[3] - p3));
        }
    }

    // 4 rectangular obstacle blocks (vertices in counterclockwise order)
    std::vector<RVO::Vector2> obs1, obs2, obs3, obs4;

    obs1.push_back(RVO::Vector2(-250.0f, 122.0f));
    obs1.push_back(RVO::Vector2(-122.0f, 122.0f));
    obs1.push_back(RVO::Vector2(-122.0f, 250.0f));
    obs1.push_back(RVO::Vector2(-250.0f, 250.0f));

    obs2.push_back(RVO::Vector2(250.0f, 122.0f));
    obs2.push_back(RVO::Vector2(250.0f, 250.0f));
    obs2.push_back(RVO::Vector2(122.0f, 250.0f));
    obs2.push_back(RVO::Vector2(122.0f, 122.0f));

    obs3.push_back(RVO::Vector2(250.0f, -122.0f));
    obs3.push_back(RVO::Vector2(122.0f, -122.0f));
    obs3.push_back(RVO::Vector2(122.0f, -250.0f));
    obs3.push_back(RVO::Vector2(250.0f, -250.0f));

    obs4.push_back(RVO::Vector2(-250.0f, -122.0f));
    obs4.push_back(RVO::Vector2(-250.0f, -250.0f));
    obs4.push_back(RVO::Vector2(-122.0f, -250.0f));
    obs4.push_back(RVO::Vector2(-122.0f, -122.0f));

    sim->addObstacle(obs1);
    sim->addObstacle(obs2);
    sim->addObstacle(obs3);
    sim->addObstacle(obs4);
    sim->processObstacles();
}

static void setPreferredVelocities(RVO::RVOSimulator*               sim,
                                   const std::vector<RVO::Vector2>& goals)
{
    for (int i = 0; i < NUM_AGENTS; ++i)
    {
        RVO::Vector2 goalVec = goals[i] - sim->getAgentPosition(i);
        if (RVO::absSq(goalVec) > 1.0f)
            goalVec = RVO::normalize(goalVec) * 10;
        sim->setAgentPrefVelocity(i, goalVec);

        // Perturb slightly to break perfect symmetry and avoid deadlocks
        float angle = static_cast<float>(std::rand()) * RVO_TWO_PI /
        static_cast<float>(RAND_MAX);
        float dist = static_cast<float>(std::rand()) * 0.0001f /
        static_cast<float>(RAND_MAX);
        sim->setAgentPrefVelocity(i, sim->getAgentPrefVelocity(i) + dist * RVO::Vector2(std::cos(angle), std::sin(angle)));
    }
}

static bool reachedGoal(RVO::RVOSimulator*               sim,
                        const std::vector<RVO::Vector2>& goals)
{
    for (std::size_t i = 0U; i < (std::size_t)NUM_AGENTS; ++i)
    {
        if (RVO::absSq(sim->getAgentPosition(i) - goals[i]) > 1200.0f)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "RVO2 — Blocks Example");

    Texture2D                 agent_texture = LoadTexture("bin/resources/agent.png");
    Texture2D                 box_texture = LoadTexture("bin/resources/box128.png");

    Color                     bgColor = { 20, 20, 30, 255 };
    Color                     obstFill = { 55, 50, 80, 255 };
    Color                     obstLine = { 140, 130, 200, 255 };
    Color                     velColor = { 80, 140, 220, 180 };
    Color                     hudColor = { 200, 200, 210, 255 };
    Color                     doneColor = { 0, 220, 0, 255 };

    RVO::RVOSimulator*        sim = new RVO::RVOSimulator();
    std::vector<RVO::Vector2> goals;
    std::vector<float>        initialDists;
    setupScenario(sim, goals, initialDists);

    bool               goalReached = false;
    int                resetCounter = 0;
    int                stepCount = 0;
    bool               showVelocity = true;
    int                goalCheckCtr = 0;

    std::vector<float> agentRotations(NUM_AGENTS, 0.0f); // last facing angle per agent

    // RenderTexture2D    staticTex = LoadRenderTexture(SCREEN_W, SCREEN_H);

    Camera2D camera = { { SCREEN_W / 2.0f, SCREEN_H / 2.0f }, { 0.0f, 0.0f }, 0.0f, 0.5f };

    double   simMs = 0.0;
    double   drawMs = 0.0;

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

        if (IsKeyPressed(KEY_V))
            showVelocity = !showVelocity;

        if (IsKeyPressed(KEY_R) || (goalReached && resetCounter <= 0))
        {
            delete sim;
            sim = new RVO::RVOSimulator();
            setupScenario(sim, goals, initialDists);
            agentRotations.assign(NUM_AGENTS, 0.0f);
            goalReached = false;
            resetCounter = 0;
            stepCount = 0;
            goalCheckCtr = 0;
        }

        // Simulation step (timed)
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
                    resetCounter = RESET_DELAY;
                }
            }
        }
        else
        {
            --resetCounter;
        }
        simMs = (GetTime() - t0) * 1000.0;

        // Draw (timed)
        double t1 = GetTime();
        BeginDrawing();

        ClearBackground(Color { 30, 30, 36, 255 });

        // Blit static texture (RenderTexture Y-flip: negative height in src rect)
        // DrawTextureRec(staticTex.texture,
        //                { 0, 0, (float)staticTex.texture.width, -(float)staticTex.texture.height },
        //                { 0, 0 },
        //                WHITE);

        BeginMode2D(camera);

        // Pass 1: velocity lines (all GL_LINES — one batch flush)
        /*  if (showVelocity)
          {
              for (int i = 0; i < NUM_AGENTS; ++i)
              {
                  RVO::Vector2 pos = sim->getAgentPosition(i);
                  RVO::Vector2 vel = sim->getAgentVelocity(i);
                  if (RVO::abs(vel) > 0.01f)
                  {
                      ::Vector2 sp = worldToScreen(pos);
                      ::Vector2 ep = { sp.x + vel.x() * WORLD_SCALE * 3.0f,
                                       sp.y - vel.y() * WORLD_SCALE * 3.0f };
                      DrawLineV(sp, ep, velColor);
                  }
              }
          }*/

        // Pass 2: agent circles (all GL_TRIANGLES — ~15 batch flushes)

        Rectangle src = { 0, 0, (float)agent_texture.width, (float)agent_texture.height };

        for (int i = 0; i < NUM_AGENTS; ++i)
        {
            RVO::Vector2 pos = sim->getAgentPosition(i);
            RVO::Vector2 vel = sim->getAgentVelocity(i);
            float        dist = RVO::abs(goals[i] - pos);
            float        prog = 1.0f - dist / initialDists[i];
            int          g = i % 4;
            Color        col = GROUP_NEAR[g]; // lerpColor(GROUP_FAR[g], GROUP_NEAR[g], prog);
            //  DrawCircleV(worldToScreen(pos), agentScreenRadius, col);

            // Update facing angle only when actually moving
            if (RVO::abs(vel) > 0.1f)
                agentRotations[i] = atan2f(vel.y(), vel.x()) * RAD2DEG;

            // Scale sprite to match agent radius (base radius = 30)
            float     scale = 1;
            float     dw = agent_texture.width * scale;
            float     dh = agent_texture.height * scale;

            ::Vector2 wp = { pos.x(), pos.y() };
            Rectangle dst = { wp.x, wp.y, dw, dh };
            DrawTexturePro(agent_texture, src, dst, { dw / 2.0f, dh / 2.0f }, agentRotations[i], col);
        }

        for (int g = 0; g < 4; ++g)
        {
            ::Vector2 gs = { GROUP_GOALS[g].x(), GROUP_GOALS[g].y() };
            Color     gc = GROUP_NEAR[g];
            gc.a = 200;
            DrawCircleLinesV(gs, 12.0f, gc);
            DrawLineV({ gs.x - 8.0f, gs.y }, { gs.x + 8.0f, gs.y }, gc);
            DrawLineV({ gs.x, gs.y - 8.0f }, { gs.x, gs.y + 8.0f }, gc);
        }

        for (int i = 0; i < 4; ++i)
        {
            ::Vector2 tl = { OBS_TL[i].x(), OBS_TL[i].y() };
            DrawRectangleV(tl, { obstW, obstH }, obstFill);
            DrawRectangleLinesEx({ tl.x, tl.y, obstW, obstH }, 2.0f, obstLine);
        }

        EndMode2D();

        // HUD
        char buf[128];
        snprintf(buf, sizeof(buf), "T: %.2f\nSteps: %d\nAgents: %d\nsim: %.2fms\ndraw: %.2fms", sim->getGlobalTime(), stepCount, NUM_AGENTS, simMs, drawMs);
        DrawText(buf, 10, 30, 18, hudColor);

        const char* hint = showVelocity ? "R: Reset   V: Hide velocity   ESC: Quit" : "R: Reset   V: Show velocity   ESC: Quit";
        //  DrawText(hint, 10, (int)ch - 24, 14, obstLine);

        /*if (goalReached)
        {
            const char* doneText = "ALL AGENTS REACHED GOAL";
            int         tw = MeasureText(doneText, 28);
            DrawText(doneText, (int)(cw / 2) - tw / 2, (int)(ch / 2) - 14, 28, doneColor);
        }*/

        if (goalReached)
        {
            const char* doneText = "ALL AGENTS REACHED GOAL";
            int         tw = MeasureText(doneText, 28);
            DrawText(doneText, (int)(SCREEN_W / 2) - tw / 2, (int)(SCREEN_H / 2) - 14, 28, doneColor);
        }

        DrawFPS(10, 10);
        EndDrawing();

        drawMs = (GetTime() - t1) * 1000.0;
    }

    // UnloadRenderTexture(staticTex);
    delete sim;
    CloseWindow();
    return 0;
}
