#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include "raylib.h"

/**
 * Atualiza a camera da viewport do editor.
 * Quando flyModeActive for falso, usa orbita:
 * - Shift + Botao Meio: Pan
 * - Botao Meio: Rotacao
 * - Scroll: Zoom
 * Quando flyModeActive for verdadeiro, usa navegacao:
 * - Botao Direito + Mouse: Olhar
 * - WASD: Movimento
 * - Scroll: Ajusta velocidade e sensibilidade
 * - Ctrl + Scroll: Zoom
 */
void UpdateEditorViewportCamera(Camera *cam, bool flyModeActive);

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
 * Sincroniza o estado interno com a camera atual.
 * Use quando a camera for alterada externamente (ex.: ao carregar projeto).
 */
void SyncCameraControllerToCamera(const Camera *cam);

#endif // CAMERA_CONTROLLER_H
