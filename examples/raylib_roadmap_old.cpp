/*
 * raylib_roadmap.cpp
 * RVO2 Library — Raylib Visualization
 *
 * Real-time visualization of the Roadmap example:
 * 100 agents in four corner groups navigate through narrow obstacle passages
 * guided by a precomputed roadmap graph (visibility graph + Dijkstra).
 *
 * Controls:
 *   R      — reset simulation
 *   V      — toggle velocity lines
 *   G      — toggle roadmap graph overlay
 *   ESC    — quit
 *   Scroll — zoom
 *
 * Rendering notes:
 *   - Static layers (obstacles + optional roadmap graph + goal markers) are
 *     cached in a RenderTexture2D, rebuilt only on reset or G toggle.
 *   - Agent velocity lines and circles are drawn in separate passes to avoid
 *     per-agent GPU batch flushes from GL_LINES <-> GL_TRIANGLES alternation.
 */

#include "RVO.h"
#include "raylib.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <map>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const int   SCREEN_W = 900;
static const int   SCREEN_H = 900;
static const float CENTER_X = SCREEN_W / 2.0f;
static const float CENTER_Y = SCREEN_H / 2.0f;
static const float WORLD_SCALE = 4.0f; // world units → pixels
static const float RVO_TWO_PI = 6.28318530717958647692f;
static const int   NUM_AGENTS = 100;
static const int   RESET_DELAY = 120; // frames to wait after goal reached

// ---------------------------------------------------------------------------
// Roadmap vertex
// ---------------------------------------------------------------------------
struct RoadmapVertex
{
    RVO::Vector2       position;
    std::vector<int>   neighbors;
    std::vector<float> distToGoal; // one entry per goal (indices 0–3)
};

// ---------------------------------------------------------------------------
// Coordinate helper
// ---------------------------------------------------------------------------
static inline ::Vector2 worldToScreen(const RVO::Vector2& v)
{
    return { CENTER_X + v.x() * WORLD_SCALE,
             CENTER_Y - v.y() * WORLD_SCALE };
}

// ---------------------------------------------------------------------------
// Color helper
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

// Goal positions match roadmap vertex indices 0–3
static const RVO::Vector2 GROUP_GOALS[4] = {
    RVO::Vector2(-75.0f, -75.0f),
    RVO::Vector2(75.0f, -75.0f),
    RVO::Vector2(-75.0f, 75.0f),
    RVO::Vector2(75.0f, 75.0f),
};

// ---------------------------------------------------------------------------
// Scenario setup
// ---------------------------------------------------------------------------
static void setupScenario(RVO::RVOSimulator*          sim,
                          std::vector<RoadmapVertex>& roadmap,
                          std::vector<int>&           goals,
                          std::vector<float>&         initialDists)
{
    std::srand(static_cast<unsigned int>(std::time(NULL)));
    roadmap.clear();
    goals.clear();
    initialDists.clear();

    sim->setTimeStep(0.25f);

    // 4 rectangular obstacle blocks (vertices in counterclockwise order)
    std::vector<RVO::Vector2> obs1, obs2, obs3, obs4;

    obs1.push_back(RVO::Vector2(-10.0f, 40.0f));
    obs1.push_back(RVO::Vector2(-40.0f, 40.0f));
    obs1.push_back(RVO::Vector2(-40.0f, 10.0f));
    obs1.push_back(RVO::Vector2(-10.0f, 10.0f));

    obs2.push_back(RVO::Vector2(10.0f, 40.0f));
    obs2.push_back(RVO::Vector2(10.0f, 10.0f));
    obs2.push_back(RVO::Vector2(40.0f, 10.0f));
    obs2.push_back(RVO::Vector2(40.0f, 40.0f));

    obs3.push_back(RVO::Vector2(10.0f, -40.0f));
    obs3.push_back(RVO::Vector2(40.0f, -40.0f));
    obs3.push_back(RVO::Vector2(40.0f, -10.0f));
    obs3.push_back(RVO::Vector2(10.0f, -10.0f));

    obs4.push_back(RVO::Vector2(-10.0f, -40.0f));
    obs4.push_back(RVO::Vector2(-10.0f, -10.0f));
    obs4.push_back(RVO::Vector2(-40.0f, -10.0f));
    obs4.push_back(RVO::Vector2(-40.0f, -40.0f));

    sim->addObstacle(obs1);
    sim->addObstacle(obs2);
    sim->addObstacle(obs3);
    sim->addObstacle(obs4);
    sim->processObstacles();

    // Roadmap vertices — first 4 are the 4 goal positions
    RoadmapVertex v;
    v.position = RVO::Vector2(-75.0f, -75.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(75.0f, -75.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(-75.0f, 75.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(75.0f, 75.0f);
    roadmap.push_back(v);

    // Obstacle-corner waypoints — 4×4 grid at (±42, ±8)
    v.position = RVO::Vector2(-42.0f, -42.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(-42.0f, -8.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(-42.0f, 8.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(-42.0f, 42.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(-8.0f, -42.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(-8.0f, -8.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(-8.0f, 8.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(-8.0f, 42.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(8.0f, -42.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(8.0f, -8.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(8.0f, 8.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(8.0f, 42.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(42.0f, -42.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(42.0f, -8.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(42.0f, 8.0f);
    roadmap.push_back(v);
    v.position = RVO::Vector2(42.0f, 42.0f);
    roadmap.push_back(v);

    sim->setAgentDefaults(15.0f, 10U, 5.0f, 5.0f, 2.0f, 2.0f);

    for (std::size_t i = 0U; i < 5U; ++i)
    {
        for (std::size_t j = 0U; j < 5U; ++j)
        {
            float        fi = static_cast<float>(i);
            float        fj = static_cast<float>(j);

            RVO::Vector2 p0(55.0f + fi * 10.0f, 55.0f + fj * 10.0f);
            sim->addAgent(p0);
            goals.push_back(0);
            initialDists.push_back(RVO::abs(GROUP_GOALS[0] - p0));

            RVO::Vector2 p1(-55.0f - fi * 10.0f, 55.0f + fj * 10.0f);
            sim->addAgent(p1);
            goals.push_back(1);
            initialDists.push_back(RVO::abs(GROUP_GOALS[1] - p1));

            RVO::Vector2 p2(55.0f + fi * 10.0f, -55.0f - fj * 10.0f);
            sim->addAgent(p2);
            goals.push_back(2);
            initialDists.push_back(RVO::abs(GROUP_GOALS[2] - p2));

            RVO::Vector2 p3(-55.0f - fi * 10.0f, -55.0f - fj * 10.0f);
            sim->addAgent(p3);
            goals.push_back(3);
            initialDists.push_back(RVO::abs(GROUP_GOALS[3] - p3));
        }
    }
}

// ---------------------------------------------------------------------------
// Build roadmap — visibility graph + 4×Dijkstra
// ---------------------------------------------------------------------------
static void buildRoadmap(RVO::RVOSimulator*          sim,
                         std::vector<RoadmapVertex>& roadmap)
{
    int n = static_cast<int>(roadmap.size());

    for (int i = 0; i < n; ++i)
    {
        roadmap[i].neighbors.clear();
        for (int j = 0; j < n; ++j)
        {
            if (sim->queryVisibility(roadmap[i].position,
                                     roadmap[j].position,
                                     sim->getAgentRadius(0U)))
            {
                roadmap[i].neighbors.push_back(j);
            }
        }
        roadmap[i].distToGoal.assign(4U, std::numeric_limits<float>::infinity());
    }

    // Dijkstra from each of the 4 goal vertices
    for (int g = 0; g < 4; ++g)
    {
        std::multimap<float, int>                        Q;
        std::vector<std::multimap<float, int>::iterator> posInQ(n, Q.end());

        roadmap[g].distToGoal[g] = 0.0f;
        posInQ[g] = Q.insert(std::make_pair(0.0f, g));

        while (!Q.empty())
        {
            const int u = Q.begin()->second;
            Q.erase(Q.begin());
            posInQ[u] = Q.end();

            for (int k = 0; k < static_cast<int>(roadmap[u].neighbors.size()); ++k)
            {
                const int   nb = roadmap[u].neighbors[k];
                const float distUV = RVO::abs(roadmap[nb].position - roadmap[u].position);

                if (roadmap[nb].distToGoal[g] > roadmap[u].distToGoal[g] + distUV)
                {
                    roadmap[nb].distToGoal[g] = roadmap[u].distToGoal[g] + distUV;

                    if (posInQ[nb] == Q.end())
                        posInQ[nb] = Q.insert(std::make_pair(roadmap[nb].distToGoal[g], nb));
                    else
                    {
                        Q.erase(posInQ[nb]);
                        posInQ[nb] = Q.insert(std::make_pair(roadmap[nb].distToGoal[g], nb));
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Preferred velocities — navigate via roadmap
// ---------------------------------------------------------------------------
static void setPreferredVelocities(RVO::RVOSimulator*                sim,
                                   const std::vector<RoadmapVertex>& roadmap,
                                   const std::vector<int>&           goals)
{
    for (int i = 0; i < NUM_AGENTS; ++i)
    {
        float minDist = std::numeric_limits<float>::infinity();
        int   minVertex = -1;

        for (int j = 0; j < static_cast<int>(roadmap.size()); ++j)
        {
            float d = RVO::abs(roadmap[j].position - sim->getAgentPosition(i)) + roadmap[j].distToGoal[goals[i]];

            if (d < minDist &&
                sim->queryVisibility(sim->getAgentPosition(i),
                                     roadmap[j].position,
                                     sim->getAgentRadius(i)))
            {
                minDist = d;
                minVertex = j;
            }
        }

        if (minVertex == -1)
        {
            sim->setAgentPrefVelocity(i, RVO::Vector2(0.0f, 0.0f));
        }
        else
        {
            if (RVO::absSq(roadmap[minVertex].position - sim->getAgentPosition(i)) == 0.0f)
            {
                if (minVertex == goals[i])
                    sim->setAgentPrefVelocity(i, RVO::Vector2());
                else
                    sim->setAgentPrefVelocity(i,
                                              RVO::normalize(roadmap[goals[i]].position - sim->getAgentPosition(i)));
            }
            else
            {
                sim->setAgentPrefVelocity(i,
                                          RVO::normalize(roadmap[minVertex].position - sim->getAgentPosition(i)));
            }
        }

        // Perturb slightly to break perfect symmetry
        float angle = static_cast<float>(std::rand()) * RVO_TWO_PI /
        static_cast<float>(RAND_MAX);
        float dist = static_cast<float>(std::rand()) * 0.0001f /
        static_cast<float>(RAND_MAX);
        sim->setAgentPrefVelocity(i, sim->getAgentPrefVelocity(i) + dist * RVO::Vector2(std::cos(angle), std::sin(angle)));
    }
}

// ---------------------------------------------------------------------------
// Goal check
// ---------------------------------------------------------------------------
static bool reachedGoal(RVO::RVOSimulator*                sim,
                        const std::vector<RoadmapVertex>& roadmap,
                        const std::vector<int>&           goals)
{
    for (std::size_t i = 0U; i < (std::size_t)NUM_AGENTS; ++i)
    {
        if (RVO::absSq(sim->getAgentPosition(i) - roadmap[goals[i]].position) > 400.0f)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Static texture — background, obstacles, optional roadmap graph, goal markers
// ---------------------------------------------------------------------------
static void buildStaticTexture(RenderTexture2D&                  tex,
                               const std::vector<RoadmapVertex>& roadmap,
                               bool                              showGraph,
                               Color                             bgColor,
                               Color                             obstFill,
                               Color                             obstLine)
{
    static const RVO::Vector2 OBS_TL[4] = {
        RVO::Vector2(-40.0f, 40.0f),
        RVO::Vector2(10.0f, 40.0f),
        RVO::Vector2(10.0f, -10.0f),
        RVO::Vector2(-40.0f, -10.0f),
    };
    float obstW = 30.0f * WORLD_SCALE;
    float obstH = 30.0f * WORLD_SCALE;

    BeginTextureMode(tex);
    ClearBackground(bgColor);

    // Obstacle blocks
    for (int i = 0; i < 4; ++i)
    {
        ::Vector2 tl = worldToScreen(OBS_TL[i]);
        DrawRectangleV(tl, { obstW, obstH }, obstFill);
        DrawRectangleLinesEx({ tl.x, tl.y, obstW, obstH }, 2.0f, obstLine);
    }

    if (showGraph && !roadmap.empty())
    {
        Color edgeColor = { 60, 110, 160, 110 };
        Color nodeColor = { 100, 160, 220, 200 };

        // Roadmap edges (draw each undirected edge once: i < j)
        for (int i = 0; i < static_cast<int>(roadmap.size()); ++i)
        {
            for (int nb : roadmap[i].neighbors)
            {
                if (nb > i)
                {
                    DrawLineV(worldToScreen(roadmap[i].position),
                              worldToScreen(roadmap[nb].position),
                              edgeColor);
                }
            }
        }

        // Roadmap vertices
        for (int i = 0; i < static_cast<int>(roadmap.size()); ++i)
        {
            ::Vector2 sp = worldToScreen(roadmap[i].position);
            if (i < 4)
            {
                // Goal vertices — colored per group, larger dot
                Color gc = GROUP_NEAR[i];
                gc.a = 220;
                DrawCircleV(sp, 6.0f, gc);
                DrawCircleLinesV(sp, 10.0f, gc);
                DrawLineV({ sp.x - 7.0f, sp.y }, { sp.x + 7.0f, sp.y }, gc);
                DrawLineV({ sp.x, sp.y - 7.0f }, { sp.x, sp.y + 7.0f }, gc);
            }
            else
            {
                // Waypoint vertices
                DrawCircleV(sp, 3.0f, nodeColor);
            }
        }
    }
    else
    {
        // Graph hidden — still draw goal markers so destinations are visible
        for (int g = 0; g < 4; ++g)
        {
            ::Vector2 gs = worldToScreen(GROUP_GOALS[g]);
            Color     gc = GROUP_NEAR[g];
            gc.a = 200;
            DrawCircleLinesV(gs, 12.0f, gc);
            DrawLineV({ gs.x - 8.0f, gs.y }, { gs.x + 8.0f, gs.y }, gc);
            DrawLineV({ gs.x, gs.y - 8.0f }, { gs.x, gs.y + 8.0f }, gc);
        }
    }

    // Center dot
    DrawCircleV({ CENTER_X, CENTER_Y }, 3.0f, { 120, 120, 140, 180 });

    EndTextureMode();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "RVO2 — Roadmap Example");

    Color                      bgColor = { 20, 20, 30, 255 };
    Color                      obstFill = { 55, 50, 80, 255 };
    Color                      obstLine = { 140, 130, 200, 255 };
    Color                      velColor = { 80, 140, 220, 180 };
    Color                      hudColor = { 200, 200, 210, 255 };
    Color                      doneColor = { 0, 220, 0, 255 };

    RVO::RVOSimulator*         sim = new RVO::RVOSimulator();
    std::vector<RoadmapVertex> roadmap;
    std::vector<int>           goals;
    std::vector<float>         initialDists;
    setupScenario(sim, roadmap, goals, initialDists);
    buildRoadmap(sim, roadmap);

    bool            goalReached = false;
    int             resetCounter = 0;
    int             stepCount = 0;
    bool            showVelocity = true;
    bool            showGraph = true;
    int             goalCheckCtr = 0;

    RenderTexture2D staticTex = LoadRenderTexture(SCREEN_W, SCREEN_H);
    buildStaticTexture(staticTex, roadmap, showGraph, bgColor, obstFill, obstLine);

    Camera2D camera = {};
    camera.offset = { CENTER_X, CENTER_Y };
    camera.target = { CENTER_X, CENTER_Y };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;
    float  userZoom = 1.0f;

    double simMs = 0.0;
    double drawMs = 0.0;

    float  agentScreenRadius = sim->getAgentRadius(0) * WORLD_SCALE;

    while (!WindowShouldClose())
    {
        // Window-fill scaling
        float cw = (float)GetScreenWidth();
        float ch = (float)GetScreenHeight();
        float sx = cw / SCREEN_W;
        float sy = ch / SCREEN_H;
        camera.zoom = ((sx > sy) ? sx : sy) * userZoom;
        camera.offset = { cw / 2.0f, ch / 2.0f };

        // Input
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
        if (IsKeyPressed(KEY_G))
        {
            showGraph = !showGraph;
            buildStaticTexture(staticTex, roadmap, showGraph, bgColor, obstFill, obstLine);
        }

        if (IsKeyPressed(KEY_R) || (goalReached && resetCounter <= 0))
        {
            delete sim;
            sim = new RVO::RVOSimulator();
            setupScenario(sim, roadmap, goals, initialDists);
            buildRoadmap(sim, roadmap);
            goalReached = false;
            resetCounter = 0;
            stepCount = 0;
            goalCheckCtr = 0;
            buildStaticTexture(staticTex, roadmap, showGraph, bgColor, obstFill, obstLine);
        }

        // Simulation step (timed)
        double t0 = GetTime();
        if (!goalReached)
        {
            setPreferredVelocities(sim, roadmap, goals);
            sim->doStep();
            ++stepCount;
            ++goalCheckCtr;

            if (goalCheckCtr >= 30)
            {
                goalCheckCtr = 0;
                if (reachedGoal(sim, roadmap, goals))
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
        DrawTextureRec(staticTex.texture,
                       { 0, 0, (float)staticTex.texture.width, -(float)staticTex.texture.height },
                       { 0, 0 },
                       WHITE);

        BeginMode2D(camera);

        // Pass 1: velocity lines (all GL_LINES — one batch)
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

        // Pass 2: agent circles (all GL_TRIANGLES — ~15 batch flushes)
        for (int i = 0; i < NUM_AGENTS; ++i)
        {
            RVO::Vector2 pos = sim->getAgentPosition(i);
            float        dist = RVO::abs(roadmap[goals[i]].position - pos);
            float        prog = 1.0f - dist / initialDists[i];
            int          g = i % 4;
            Color        col = lerpColor(GROUP_FAR[g], GROUP_NEAR[g], prog);
            DrawCircleV(worldToScreen(pos), agentScreenRadius, col);
        }

        EndMode2D();

        // HUD
        char buf[128];
        snprintf(buf, sizeof(buf), "T: %.2f  Steps: %d  Agents: %d  sim: %.2fms  draw: %.2fms", sim->getGlobalTime(), stepCount, NUM_AGENTS, simMs, drawMs);
        DrawText(buf, 10, 10, 18, hudColor);

        const char* hint = showVelocity ? "R: Reset   V: Hide vel   G: Toggle graph   ESC: Quit" : "R: Reset   V: Show vel   G: Toggle graph   ESC: Quit";
        DrawText(hint, 10, (int)ch - 24, 14, obstLine);

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
