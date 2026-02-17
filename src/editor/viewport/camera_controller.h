#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include "raylib.h"

/**
 * Atualiza a câmera com controles estilo Blender:
 * - Shift + Botão Meio: Pan (movimento da visão)
 * - Botão Meio: Rotação
 * - Scroll: Zoom
 * - Botão direito: Orbita
 */
void UpdateCameraBlender(Camera *cam);

/**
 * Habilita o cursor para que o usuário possa interagir com a UI
 */
void EnableMouseForUI(void);

/**
 * Desabilita o cursor para que a câmera funcione
 */
void DisableMouseForUI(void);

/**
 * Retorna verdadeiro se o cursor está habilitado para UI
 */
bool IsMouseEnabledForUI(void);

/**
 * Inicializa a câmera com posição padrão (6, 6, 6) e target em (0, 0, 0)
 * Retorna uma câmera configurada pronta para uso
 */
Camera InitCamera();

/**
 * Sincroniza o estado interno de orbita (yaw/pitch/distancia) com a camera atual.
 * Use quando a camera for alterada externamente (ex.: ao carregar projeto).
 */
void SyncCameraControllerToCamera(const Camera *cam);

#endif // CAMERA_CONTROLLER_H
