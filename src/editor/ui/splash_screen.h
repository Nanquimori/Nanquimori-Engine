#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

#include "raylib.h"

typedef struct
{
    Texture2D splash;
    Texture2D iconYT;
    bool mostrar;
    bool bloquearFechar;
    bool texturaOk;
    bool hoverYT;
} SplashScreen;

// Inicializar splash screen
void InitSplashScreen(void);

// Atualizar estado do splash screen
void UpdateSplashScreen(void);

// Desenhar splash screen
void DrawSplashScreen(void);

// Verificar se deve mostrar
bool SplashScreenShouldShow(void);
bool SplashScreenIsInputBlocked(void);

// Mostrar splash screen
void ShowSplashScreen(void);

// Fechar splash screen
void CloseSplashScreen(void);

// Descarregar recursos
void UnloadSplashScreen(void);

#endif // SPLASH_SCREEN_H
