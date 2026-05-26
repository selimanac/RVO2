#include "raylib.h"

static const int SCREEN_W = 1920;
static const int SCREEN_H = 1280;

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    // Init raylib
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "FABRIK-C");

    Texture2D agent_texture = LoadTexture("bin/resources/agent.png");

    // LOOP
    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(Color { 30, 30, 36, 255 });

        DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
