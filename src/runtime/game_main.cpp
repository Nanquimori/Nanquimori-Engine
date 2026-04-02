#include "game_application.h"
#include "raylib.h"

int main(void)
{
    InitializeGameApplication();

    while (!WindowShouldClose())
    {
        UpdateGameApplication();
        RenderGameApplication();
    }

    ShutdownGameApplication();
    return 0;
}
