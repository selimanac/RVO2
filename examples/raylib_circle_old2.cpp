/*
 * raylib_circle.cpp
 * RVO2 Library — Raylib Visualization
 *
 * Real-time visualization of the Circle example:
 * N agents evenly distributed on a circle attempting to move to
 * the antipodal position, rendered with Raylib.
 *
 * Controls:
 *   R      — reset simulation
 *   V      — toggle velocity lines
 *   ESC    — quit
 *   Scroll — zoom
 *
 * Rendering notes:
 *   - Static layers (formation ring + goal markers) are cached in a RenderTexture2D
 *     and redrawn only on reset, not every frame.
 *   - Agent velocity lines and circles are drawn in separate passes to avoid
 *     per-agent GPU batch flushes from GL_LINES <-> GL_TRIANGLES alternation.
 */

#include "RVO.h"
#include "raylib.h"

#include <cmath>
#include <cstddef>
#include <vector>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const int   SCREEN_W = 1920;
static const int   SCREEN_H = 1280;

static const float RVO_TWO_PI = 6.28318530717958647692f;
static const int   NUM_AGENTS = 1550;
static const float FORM_RADIUS = 2900.0f;
static const int   RESET_DELAY = 120; // frames after goal reached

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------

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
// Simulation setup
// ---------------------------------------------------------------------------
static void setupScenario(RVO::RVOSimulator* sim, std::vector<RVO::Vector2>& goals)
{
    goals.clear();
    sim->setTimeStep(0.25f);
    sim->setAgentDefaults(120.0f, 5U, 5.0f, 5.0f, 30.0f, 20.0f);

    // Regular agents evenly spaced across NUM_AGENTS slots (last slot reserved for big agent)
    for (std::size_t i = 0; i < (std::size_t)(NUM_AGENTS); ++i)
    {
        float angle = static_cast<float>(i) * RVO_TWO_PI / static_cast<float>(NUM_AGENTS);
        sim->addAgent(FORM_RADIUS * RVO::Vector2(std::cos(angle), std::sin(angle)));
        goals.push_back(-sim->getAgentPosition(i));
        
    }
}

static void setPreferredVelocities(RVO::RVOSimulator*               sim,
                                   const std::vector<RVO::Vector2>& goals)
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
    InitWindow(SCREEN_W, SCREEN_H, "RVO2 — Circle Example");

    Texture2D agent_texture = LoadTexture("bin/resources/agent.png");

    // Colors
    Color ringColor = { 60, 60, 255, 255 };
    Color agentFar = { 220, 50, 30, 255 };
    Color agentNear = { 0, 220, 80, 255 };
    Color hudColor = { 200, 200, 210, 255 };
    Color doneColor = { 0, 220, 0, 255 };

    // Simulation state
    RVO::RVOSimulator*        sim = new RVO::RVOSimulator();
    std::vector<RVO::Vector2> goals;
    setupScenario(sim, goals);

    std::vector<float> agentRotations(NUM_AGENTS, 0.0f); // last facing angle per agent

    bool               goalReached = false;
    int                resetCounter = 0;
    int                stepCount = 0;
    bool               showVelocity = true;
    int                goalCheckCtr = 0; // Fix E: lazy goal check

    // Camera
    Camera2D camera = {};
    camera.offset = { 0, 0 };
    camera.target = { 0, 0 };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;
    float userZoom = 1.0f;

    // Timing
    double simMs = 0.0;
    double drawMs = 0.0;

    while (!WindowShouldClose())
    {
        // --- Window scaling ---
        float cw = (float)GetScreenWidth();
        float ch = (float)GetScreenHeight();
        float sx = cw / SCREEN_W;
        float sy = ch / SCREEN_H;
        camera.zoom = ((sx > sy) ? sx : sy) * userZoom;
        camera.offset = { cw / 2.0f, ch / 2.0f };

        // --- Input ---
        float scroll = GetMouseWheelMove();
        if (scroll != 0.0f)
        {
            userZoom += scroll * 0.1f;
            if (userZoom < 0.2f)
                userZoom = 0.2f;
            if (userZoom > 5.0f)
                userZoom = 5.0f;
        }
        if (IsKeyPressed(KEY_Z))
            userZoom = 1.0f;
        if (IsKeyPressed(KEY_V))
            showVelocity = !showVelocity;

        if (IsKeyPressed(KEY_R) || (goalReached && resetCounter <= 0))
        {
            delete sim;
            sim = new RVO::RVOSimulator();
            setupScenario(sim, goals);
            agentRotations.assign(NUM_AGENTS, 0.0f);
            goalReached = false;
            resetCounter = 0;
            stepCount = 0;
            goalCheckCtr = 0;
            //   buildStaticTexture(staticTex, goals, bgColor, ringColor, goalColor);
        }

        // --- Simulation step (Fix D: timed) ---
        double t0 = GetTime();
        if (!goalReached)
        {
            setPreferredVelocities(sim, goals);
            sim->doStep();
            ++stepCount;
            ++goalCheckCtr;

            // Fix E: check goal only every 30 steps
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

        // --- Draw (Fix D: timed) ---
        double t1 = GetTime();
        BeginDrawing();

        ClearBackground(Color { 30, 30, 36, 255 });

        // Fix B: blit static texture (1 draw call for ring + all goal markers)
        // RenderTexture is flipped in Y — use source rect with negative height
        //   DrawTextureRec(staticTex.texture, { 0, 0, (float)staticTex.texture.width, -(float)staticTex.texture.height }, { 0, 0 }, WHITE);

        BeginMode2D(camera);

        // Fix A — Pass 1: all velocity lines (GL_LINES, one batch)
        /*  if (showVelocity)
          {
              for (int i = 0; i < NUM_AGENTS; ++i)
              {
                  RVO::Vector2 pos = sim->getAgentPosition(i);
                  RVO::Vector2 vel = sim->getAgentVelocity(i);
                  if (RVO::abs(vel) > 0.01f)
                  {
                      ::Vector2 sp = pos;
                      ::Vector2 ep = { sp.x + vel.x() * WORLD_SCALE * 3.0f,
                                       sp.y - vel.y() * WORLD_SCALE * 3.0f };
                      DrawLineV(sp, ep, velColor);
                  }
              }
          }*/

        // Pass 2: all agent sprites (GL_TRIANGLES, ~15 batches)
        {
            Rectangle src = { 0, 0, (float)agent_texture.width, (float)agent_texture.height };

            for (int i = 0; i < NUM_AGENTS; ++i)
            {
                RVO::Vector2 pos = sim->getAgentPosition(i);
                RVO::Vector2 vel = sim->getAgentVelocity(i);
                float        dist = RVO::abs(goals[i] - pos);
                float        progress = 1.0f - dist / (2.0f * FORM_RADIUS);
                Color        col = lerpColor(agentFar, agentNear, progress);

                // Update facing angle only when actually moving
                if (RVO::abs(vel) > 0.1f)
                    agentRotations[i] = atan2f(vel.y(), vel.x()) * RAD2DEG;

                // Scale sprite to match agent radius (base radius = 30)
                float     scale = sim->getAgentRadius(i) / 30.0f;
                float     dw = agent_texture.width * scale;
                float     dh = agent_texture.height * scale;
                Rectangle dst = { pos.x(), pos.y(), dw, dh };
                DrawTexturePro(agent_texture, src, dst, { dw / 2.0f, dh / 2.0f }, agentRotations[i], col);
            }
        }

        EndMode2D();

        // HUD
        char buf[96];
        snprintf(buf, sizeof(buf), "T: %.2f  Steps: %d  Agents: %d  sim: %.2fms  draw: %.2fms", sim->getGlobalTime(), stepCount, NUM_AGENTS, simMs, drawMs);
        DrawText(buf, 10, 10, 18, hudColor);

        const char* hint = showVelocity ? "R: Reset   V: Hide velocity   ESC: Quit" : "R: Reset   V: Show velocity   ESC: Quit";
        DrawText(hint, 10, (int)ch - 24, 14, ringColor);

        if (goalReached)
        {
            const char* doneText = "ALL AGENTS REACHED GOAL";
            int         tw = MeasureText(doneText, 28);
            DrawText(doneText, (int)(cw / 2) - tw / 2, (int)(ch / 2) - 14, 28, doneColor);
        }

        DrawFPS((int)cw - 90, 10);
        EndDrawing();

        drawMs = (GetTime() - t1) * 1000.0;
    }

    delete sim;
    CloseWindow();
    return 0;
}
