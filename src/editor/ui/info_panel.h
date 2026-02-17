#ifndef INFO_PANEL_H
#define INFO_PANEL_H

#include "raylib.h"

typedef struct
{
    Vector3 cameraPos;
    Vector3 cameraTarget;
    float cameraDistObservada;
    bool distHit;
    bool mostrar;
    float logicMs;
    float physicsMs;
    float renderMs;
} InfoPanel;

// Inicializar painel de informações
void InitInfoPanel(void);

// Atualizar dados do painel de informações
void UpdateInfoPanel(Vector3 cameraPos, Vector3 cameraTarget, float cameraDistObservada, bool distHit);

// Desenhar painel de informações
void DrawInfoPanel(void);

// Mostrar/ocultar painel
void SetInfoPanelVisible(bool visible);

// Obter visibilidade
bool IsInfoPanelVisible(void);

// Mostrar/ocultar linha do raycast (UI)
void SetRaycastLineVisible(bool visible);
bool IsRaycastLineVisible(void);

void SetRaycast2DVisible(bool visible);
bool IsRaycast2DVisible(void);

void SetRaycast3DVisible(bool visible);
bool IsRaycast3DVisible(void);
bool IsMouseOverInfoPanel(Vector2 mouse);

// Perfil de desempenho (estilo UPBGE)
void UpdateInfoPanelProfile(float logicMs, float physicsMs, float renderMs);

#endif // INFO_PANEL_H
