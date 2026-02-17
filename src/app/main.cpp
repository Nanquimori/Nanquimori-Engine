#include "application.h"

// -------------------------------------------------------
// PROGRAMA PRINCIPAL
// -------------------------------------------------------
int main(void)
{
    InitializeApplication();

    while (!WindowShouldClose())
    {
        UpdateApplication();
        RenderApplication();
    }

    ShutdownApplication();
    return 0;
}
