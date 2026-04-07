#ifndef TOP_BAR_H
#define TOP_BAR_H

#include "raylib.h"

// -------------------------------------------------------
// INICIALIZAR E DESCARREGAR A BARRA SUPERIOR
// -------------------------------------------------------
void InitTopBar();
void UnloadTopBar();

// -------------------------------------------------------
// ATUALIZAR E DESENHAR A BARRA SUPERIOR
// -------------------------------------------------------
void UpdateTopBar();
void DrawTopBar();
bool IsPlayModeActive(void);
void SetPlayModeActive(bool active);
bool IsPlayPaused(void);
void SetPlayPaused(bool paused);
bool ConsumePlayStopRequested(void);
bool ConsumePlayRestartRequested(void);
bool IsTopBarMenuOpen(void);
bool IsViewportNavigateModeActive(void);
bool IsViewportWireframeModeActive(void);
void ToggleViewportWireframeMode(void);
const char *GetViewportRenderModeLabel(void);

#endif // TOP_BAR_H
