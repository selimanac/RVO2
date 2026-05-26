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
static const int   SCREEN_W = 900;
static const int   SCREEN_H = 900;
static const float CENTER_X = SCREEN_W / 2.0f;
static const float CENTER_Y = SCREEN_H / 2.0f;
static const float WORLD_SCALE = 2.0f; // world units → pixels
static const float RVO_TWO_PI = 6.28318530717958647692f;
static const int   NUM_AGENTS = 2250;
static const float FORM_RADIUS = 200.0f; // world units
static const int   RESET_DELAY = 120;    // frames after goal reached

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------
static inline ::Vector2 worldToScreen(const RVO::Vector2& v)
{
    return { CENTER_X + v.x() * WORLD_SCALE,
             CENTER_Y - v.y() * WORLD_SCALE };
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

// ---------------------------------------------------------------------------
// Simulation setup
// ---------------------------------------------------------------------------
static void setupScenario(RVO::RVOSimulator* sim, std::vector<RVO::Vector2>& goals)
{
    goals.clear();
    sim->setTimeStep(0.55f);
    sim->setAgentDefaults(15.0f, 5U, 10.0f, 10.0f, 1.5f, 2.0f);

    for (std::size_t i = 0; i < (std::size_t)NUM_AGENTS; ++i)
    {
        float angle = static_cast<float>(i) * RVO_TWO_PI * (1.0f / NUM_AGENTS);
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
            goalVec = RVO::normalize(goalVec);
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
// Static layer helper — draws formation circle + goal markers into a texture
// ---------------------------------------------------------------------------
static void buildStaticTexture(RenderTexture2D&                 tex,
                               const std::vector<RVO::Vector2>& goals,
                               Color                            bgColor,
                               Color                            ringColor,
                               Color                            goalColor)
{
    float screenFormRadius = FORM_RADIUS * WORLD_SCALE;

    BeginTextureMode(tex);
    ClearBackground(bgColor);

    // Formation ring
    DrawRing({ CENTER_X, CENTER_Y },
             screenFormRadius - 1.0f,
             screenFormRadius + 1.0f,
             0.0f,
             360.0f,
             120,
             ringColor);

    // Goal markers (all GL_LINES — one batch)
    for (int i = 0; i < (int)goals.size(); ++i)
    {
        ::Vector2 gs = worldToScreen(goals[i]);
        DrawCircleLinesV(gs, 3.0f, goalColor);
    }

    // Center dot
    DrawCircleV({ CENTER_X, CENTER_Y }, 3.0f, ringColor);

    EndTextureMode();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "RVO2 — Circle Example");

    // Colors
    Color bgColor = { 20, 20, 30, 255 };
    Color ringColor = { 60, 60, 255, 255 };
    Color goalColor = { 255, 120, 120, 200 };
    Color agentFar = { 220, 50, 30, 255 };
    Color agentNear = { 0, 220, 80, 255 };
    Color velColor = { 80, 140, 220, 200 };
    Color hudColor = { 200, 200, 210, 255 };
    Color doneColor = { 0, 220, 0, 255 };

    // Simulation state
    RVO::RVOSimulator*        sim = new RVO::RVOSimulator();
    std::vector<RVO::Vector2> goals;
    setupScenario(sim, goals);

    bool goalReached = false;
    int  resetCounter = 0;
    int  stepCount = 0;
    bool showVelocity = true;
    int  goalCheckCtr = 0; // Fix E: lazy goal check

    // Fix B: static layer texture (formation ring + goal markers)
    RenderTexture2D staticTex = LoadRenderTexture(SCREEN_W, SCREEN_H);
    buildStaticTexture(staticTex, goals, bgColor, ringColor, goalColor);

    // Camera
    Camera2D camera = {};
    camera.offset = { CENTER_X, CENTER_Y };
    camera.target = { CENTER_X, CENTER_Y };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;
    float userZoom = 1.0f;

    // Timing
    double simMs = 0.0;
    double drawMs = 0.0;

    float  agentScreenRadius = sim->getAgentRadius(0) * WORLD_SCALE;
    if (agentScreenRadius < 2.0f)
        agentScreenRadius = 2.0f;

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
        DrawTextureRec(staticTex.texture, { 0, 0, (float)staticTex.texture.width, -(float)staticTex.texture.height }, { 0, 0 }, WHITE);

        BeginMode2D(camera);

        // Fix A — Pass 1: all velocity lines (GL_LINES, one batch)
        if (showVelocity)
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
        }

        // Fix A — Pass 2: all agent circles (GL_TRIANGLES, ~15 batches)
        for (int i = 0; i < NUM_AGENTS; ++i)
        {
            RVO::Vector2 pos = sim->getAgentPosition(i);
            float        dist = RVO::abs(goals[i] - pos);
            float        progress = 1.0f - dist / (2.0f * FORM_RADIUS);
            Color        col = lerpColor(agentFar, agentNear, progress);
            DrawCircleV(worldToScreen(pos), agentScreenRadius, col);
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

    UnloadRenderTexture(staticTex);
    delete sim;
    CloseWindow();
    return 0;
}
