#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include "raylib.h"

/**
 * Atualiza a camera com controles de orbita do editor:
 * - Shift + Botao Meio: Pan
 * - Botao Meio: Rotacao
 * - Scroll: Zoom
 */
void UpdateEditorOrbitCamera(Camera *cam);

/**
 * Habilita o cursor para que o usuario possa interagir com a UI
 */
void EnableMouseForUI(void);

/**
 * Desabilita o cursor para que a camera funcione
 */
void DisableMouseForUI(void);

/**
 * Retorna verdadeiro se o cursor esta habilitado para UI
 */
bool IsMouseEnabledForUI(void);

/**
 * Inicializa a camera com posicao padrao (6, 6, 6) e target em (0, 0, 0)
 * Retorna uma camera configurada pronta para uso
 */
Camera InitCamera();

/**
 * Sincroniza o estado interno de orbita (yaw/pitch/distancia) com a camera atual.
 * Use quando a camera for alterada externamente (ex.: ao carregar projeto).
 */
void SyncCameraControllerToCamera(const Camera *cam);

#endif // CAMERA_CONTROLLER_H
