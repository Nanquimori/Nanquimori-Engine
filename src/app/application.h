#ifndef APPLICATION_H
#define APPLICATION_H

#include "raylib.h"

// -------------------------------------------------------
// APLICAÇÃO - CICLO DE VIDA E GERENCIAMENTO
// -------------------------------------------------------

/**
 * Inicializa a janela e todos os módulos da aplicação
 */
void InitializeApplication();

/**
 * Limpa todos os recursos da aplicação
 */
void ShutdownApplication();

/**
 * Realiza atualização lógica de todos os módulos (input, lógica, etc)
 */
void UpdateApplication();

/**
 * Renderiza toda a cena (3D e 2D UI)
 */
void RenderApplication();

Camera GetEditorViewportCamera(void);
void SetEditorViewportCamera(const Camera *camera);
bool LookThroughSceneCameraObject(int objectId);
bool LaunchProjectPlayer(char *status, int statusSize);

#endif // APPLICATION_H
