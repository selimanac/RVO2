/*
 * raylib_movement.cpp
 * RVO2 Library — Raylib Visualization
 *
 * RTS-style movement example: select units and right-click to move them.
 * ORCA handles separation and collision avoidance automatically.
 *
 * Controls:
 *   SPACE              — spawn 10 units at origin (ORCA separates them)
 *   Left click         — select unit under cursor (deselects others)
 *   Left click + drag  — rectangle-select all units inside AABB
 *   Left click (empty) — move all selected units to that point
 *   Scroll             — zoom
 *   ESC                — quit
 */

// SPDX-FileCopyrightText: 2008 University of North Carolina at Chapel Hill
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Please send all bug reports to <geom@cs.unc.edu>.
//
// The authors may be contacted via:
//
// Jur van den Berg, Stephen J. Guy, Jamie Snape, Ming C. Lin, Dinesh Manocha
// Dept. of Computer Science
// 201 S. Columbia St.
// Frederick P. Brooks, Jr. Computer Science Bldg.
// Chapel Hill, N.C. 27599-3175
// United States of America
//
// <https://gamma.cs.unc.edu/RVO2/>

#include "RVO.h"
#include "raylib.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
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
static const float UNIT_RADIUS = 20.0f;
static const float UNIT_SPEED = 500.0f;
static const int   SPAWN_COUNT = 100;

// Drag distance (world units) needed to trigger rect-select vs. single click
static const float DRAG_THRESHOLD = 5.0f;

// Arrival: stop when within half a radius of the goal
static const float ARRIVAL_STOP_DIST = UNIT_RADIUS * 0.5f;
// Arrival: start slowing down at 3× radius
static const float ARRIVAL_SLOW_DIST = UNIT_RADIUS * 3.0f;

const float        unit_sizes[4] = { 20, 30, 40, 60 };
const float        unit_speeds[4] = { 700, 600, 500, 400 };
std::random_device rd;
std::mt19937       gen(rd());

// ---------------------------------------------------------------------------
// Unit struct
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Simulation state
// ---------------------------------------------------------------------------
static RVO::RVOSimulator* sim = 0;
static std::vector<Unit>  units;

// ---------------------------------------------------------------------------
// Selection state
// ---------------------------------------------------------------------------
static bool    isSelecting = false;
static Vector2 selStartScreen = { 0.0f, 0.0f };
static Vector2 selEndScreen = { 0.0f, 0.0f };

// ---------------------------------------------------------------------------
// Setup — configures agent defaults, no initial agents
// ---------------------------------------------------------------------------
static void setupSim()
{
    sim->setAgentDefaults(
    UNIT_SPEED * 3.0f + 2.0f * UNIT_RADIUS, // neighborDist
    10U,                                    // maxNeighbors
    100.0f,                                 // timeHorizon
    1.0f,                                   // timeHorizonObst
    UNIT_RADIUS,
    UNIT_SPEED);
}

// ---------------------------------------------------------------------------
// Spawn units in a circle so ORCA has valid separation directions immediately
// ---------------------------------------------------------------------------
static void spawnUnits(int count)
{
    for (int i = 0; i < count; ++i)
    {
        std::uniform_int_distribution<int> dist(0, 3);
        uint8_t                            random_num = static_cast<uint8_t>(dist(gen));
        float                              unit_radius = unit_sizes[random_num];
        float                              unit_speed = unit_speeds[random_num];

        float                              spawnRadius = (count * 1.2f * unit_radius) / (4.0f * 3.14159265f);
        if (spawnRadius < unit_radius * 1.2f)
            spawnRadius = unit_radius * 1.2f;

        float        angle = static_cast<float>(i) / static_cast<float>(count) * (2.0f * 3.14159265f);
        RVO::Vector2 pos(cosf(angle) * spawnRadius, sinf(angle) * spawnRadius);
        // std::size_t  idx = sim->addAgent(pos);

        std::size_t idx = sim->addAgent(pos, unit_speed * 3.0f + 2.0f * unit_radius, 10U, 10.f, 1.0f, unit_radius, unit_speed);
        Unit        u;
        u.simIdx = idx;
        u.selected = false;
        u.rotation = 0.0f;
        u.goal = RVO::Vector2(0.0f, 0.0f);
        u.hasGoal = false;
        u.unit_radius = unit_radius;
        u.unit_speed = unit_speed;
        units.push_back(u);
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "RVO2 — RTS Movement");

    // Camera fixed at world origin, zoom only
    Camera2D  camera = { { SCREEN_W / 2.0f, SCREEN_H / 2.0f }, { 0.0f, 0.0f }, 0.0f, 0.5f };

    Texture2D enemy_texture = LoadTexture("bin/resources/agent.png");

    sim = new RVO::RVOSimulator();
    setupSim();

    double simMs = 0.0;
    double drawMs = 0.0;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        if (dt <= 0.0f || dt > 0.05f)
            dt = 0.016f;
        sim->setTimeStep(dt);

        // ---- Input -------------------------------------------------------

        // Zoom
        float scroll = GetMouseWheelMove();
        if (scroll != 0.0f)
        {
            camera.zoom += scroll * 0.1f;
            if (camera.zoom < 0.05f)
                camera.zoom = 0.05f;
            if (camera.zoom > 5.0f)
                camera.zoom = 5.0f;
        }

        // Spawn
        if (IsKeyPressed(KEY_SPACE))
            spawnUnits(SPAWN_COUNT);

        // Mouse input — world-space position
        Vector2 mouseScreen = GetMousePosition();
        Vector2 mouseWorld = GetScreenToWorld2D(mouseScreen, camera);

        // Left button pressed: start selection
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            isSelecting = true;
            selStartScreen = mouseScreen;
            selEndScreen = mouseScreen;
        }

        // Left button held: update end corner
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && isSelecting)
            selEndScreen = mouseScreen;

        // Left button released: apply selection or move command
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && isSelecting)
        {
            isSelecting = false;

            Vector2 startWorld = GetScreenToWorld2D(selStartScreen, camera);
            Vector2 endWorld = GetScreenToWorld2D(selEndScreen, camera);

            float   dragDX = endWorld.x - startWorld.x;
            float   dragDY = endWorld.y - startWorld.y;
            float   dragDist = sqrtf(dragDX * dragDX + dragDY * dragDY);

            if (dragDist >= DRAG_THRESHOLD)
            {
                // Rectangle selection — build AABB
                float minX = startWorld.x < endWorld.x ? startWorld.x : endWorld.x;
                float maxX = startWorld.x > endWorld.x ? startWorld.x : endWorld.x;
                float minY = startWorld.y < endWorld.y ? startWorld.y : endWorld.y;
                float maxY = startWorld.y > endWorld.y ? startWorld.y : endWorld.y;

                for (std::size_t i = 0; i < units.size(); ++i)
                {
                    RVO::Vector2 pos = sim->getAgentPosition(units[i].simIdx);
                    units[i].selected = (pos.x() >= minX && pos.x() <= maxX &&
                                         pos.y() >= minY && pos.y() <= maxY);
                }
            }
            else
            {
                // Short click — check if any unit is under the cursor
                bool hitUnit = false;
                for (std::size_t i = 0; i < units.size(); ++i)
                {
                    RVO::Vector2 pos = sim->getAgentPosition(units[i].simIdx);
                    float        dx = pos.x() - mouseWorld.x;
                    float        dy = pos.y() - mouseWorld.y;
                    if (sqrtf(dx * dx + dy * dy) <= units[i].unit_radius)

                    {
                        // Deselect all, select only this unit
                        for (std::size_t j = 0; j < units.size(); ++j)
                            units[j].selected = false;
                        units[i].selected = true;
                        hitUnit = true;
                        break;
                    }
                }

                if (!hitUnit)
                {
                    // Click on empty space — move all selected units to this point
                    RVO::Vector2 goalPos(mouseWorld.x, mouseWorld.y);
                    for (std::size_t i = 0; i < units.size(); ++i)
                    {
                        if (units[i].selected)
                        {
                            units[i].goal = goalPos;
                            units[i].hasGoal = true;
                        }
                    }
                }
            }
        }

        // ---- Sim step ----------------------------------------------------

        double t0 = GetTime();
#ifdef _OPENMP
#pragma omp parallel for
#endif /* _OPENMP */
        for (std::size_t i = 0; i < units.size(); ++i)
        {
            /*   if (!units[i].hasGoal)
               {
                   sim->setAgentPrefVelocity(units[i].simIdx, RVO::Vector2(0.0f, 0.0f));
                   continue;
               }*/

            RVO::Vector2 pos = sim->getAgentPosition(units[i].simIdx);
            RVO::Vector2 toGoal = units[i].goal - pos;
            float        dist = RVO::abs(toGoal);

            /*      if (dist <= ARRIVAL_STOP_DIST)
                  {
                      sim->setAgentPrefVelocity(units[i].simIdx, RVO::Vector2(0.0f, 0.0f));
                      sim->setAgentVelocity(units[i].simIdx, RVO::Vector2(0.0f, 0.0f));
                      units[i].hasGoal = false;
                  }
                  else if (dist < ARRIVAL_SLOW_DIST)
                  {
                      float t = dist / ARRIVAL_SLOW_DIST; // 0..1
                      sim->setAgentPrefVelocity(units[i].simIdx, RVO::normalize(toGoal) * UNIT_SPEED * t);
                  }
                  else
                  {
                      sim->setAgentPrefVelocity(units[i].simIdx, RVO::normalize(toGoal) * UNIT_SPEED);
                  }*/

            sim->setAgentPrefVelocity(units[i].simIdx, RVO::normalize(toGoal) * units[i].unit_speed);

            // sim->setAgentPrefVelocity(units[i].simIdx, RVO::normalize(toGoal) * UNIT_SPEED);
        }

        sim->doStep();

        // Update rotations from velocity
        for (std::size_t i = 0; i < units.size(); ++i)
        {
            RVO::Vector2 vel = sim->getAgentVelocity(units[i].simIdx);
            if (RVO::abs(vel) > 0.1f)
                units[i].rotation = atan2f(vel.y(), vel.x()) * RAD2DEG;
        }

        simMs = (GetTime() - t0) * 1000.0;

        // ---- Draw --------------------------------------------------------

        double t1 = GetTime();
        BeginDrawing();
        ClearBackground(Color { 30, 30, 36, 255 });

        BeginMode2D(camera);

        // Draw goal markers for selected units
        for (std::size_t i = 0; i < units.size(); ++i)
        {
            if (units[i].selected && units[i].hasGoal)
            {
                DrawCircleLines(
                (int)units[i].goal.x(),
                (int)units[i].goal.y(),
                units[i].unit_radius * 0.6f,
                Color { 100, 255, 100, 180 });
                // Small crosshair
                DrawLine(
                (int)units[i].goal.x() - (int)(units[i].unit_radius * 0.4f),
                (int)units[i].goal.y(),
                (int)units[i].goal.x() + (int)(units[i].unit_radius * 0.4f),
                (int)units[i].goal.y(),
                Color { 100, 255, 100, 180 });
                DrawLine(
                (int)units[i].goal.x(),
                (int)units[i].goal.y() - (int)(units[i].unit_radius * 0.4f),
                (int)units[i].goal.x(),
                (int)units[i].goal.y() + (int)(units[i].unit_radius * 0.4f),
                Color { 100, 255, 100, 180 });
                break; // All selected units share the same goal — draw once
            }
        }

        // Draw units
        float ew = (float)enemy_texture.width;
        float eh = (float)enemy_texture.height;

        for (std::size_t i = 0; i < units.size(); ++i)
        {
            float        escale = (units[i].unit_radius * 2.0f) / ew;
            float        dw = ew * escale;
            float        dh = eh * escale;

            RVO::Vector2 pos = sim->getAgentPosition(units[i].simIdx);
            Color        tint = units[i].selected ? Color { 80, 230, 80, 255 } : WHITE;

            DrawTexturePro(
            enemy_texture,
            Rectangle { 0.0f, 0.0f, ew, eh },
            Rectangle { pos.x(), pos.y(), dw, dh },
            Vector2 { dw / 2.0f, dh / 2.0f },
            units[i].rotation,
            tint);

            // Selection ring for selected units
            if (units[i].selected)
                DrawCircleLines((int)pos.x(), (int)pos.y(), units[i].unit_radius + 3.0f, Color { 80, 230, 80, 200 });
        }

        // Selection rectangle (world space) while dragging
        if (isSelecting)
        {
            Vector2 sw = GetScreenToWorld2D(selStartScreen, camera);
            Vector2 ew2 = GetScreenToWorld2D(selEndScreen, camera);

            float   rx = sw.x < ew2.x ? sw.x : ew2.x;
            float   ry = sw.y < ew2.y ? sw.y : ew2.y;
            float   rw = sw.x < ew2.x ? ew2.x - sw.x : sw.x - ew2.x;
            float   rh = sw.y < ew2.y ? ew2.y - sw.y : sw.y - ew2.y;

            DrawRectangle((int)rx, (int)ry, (int)rw, (int)rh, Color { 80, 230, 80, 30 });
            DrawRectangleLinesEx(Rectangle { rx, ry, rw, rh }, 1.5f / camera.zoom, Color { 80, 230, 80, 200 });
        }

        EndMode2D();

        // Count selected
        int selectedCount = 0;
        for (std::size_t i = 0; i < units.size(); ++i)
            if (units[i].selected)
                ++selectedCount;

        // HUD
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Units: %zu  Selected: %d\nSim: %.2fms  Draw: %.2fms\n"
                 "[SPACE] spawn  [LClick] select  [LDrag] rect-select  [LClick empty] move",
                 units.size(),
                 selectedCount,
                 simMs,
                 drawMs);
        DrawFPS(10, 10);
        DrawText(buf, 10, 40, 18, Color { 200, 200, 210, 255 });

        EndDrawing();
        drawMs = (GetTime() - t1) * 1000.0;
    }

    UnloadTexture(enemy_texture);
    delete sim;
    CloseWindow();
    return 0;
}
