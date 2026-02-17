#ifndef GIZMO_H
#define GIZMO_H

#include "raylib.h"

#define GIZMO_RAIO 60.0f
#define GIZMO_MARGEM 45.0f

#define COR_FUNDO (Color){14, 14, 16, 255}
#define COR_PAINEL (Color){20, 20, 22, 255}
#define COR_BORDA (Color){54, 54, 58, 255}

/* Z = AZUL */
#define COR_NORTE (Color){80, 140, 255, 255} // -Z
#define COR_SUL (Color){80, 140, 255, 255}   // +Z

/* X = VERMELHO */
#define COR_LESTE (Color){184, 38, 36, 255} // +X
#define COR_OESTE (Color){184, 38, 36, 255} // -X

#define COR_X_POS (Color){184, 38, 36, 255}
#define COR_X_NEG (Color){184, 38, 36, 255}

/* Y = NEUTRO (AZUL CLARO) */
#define COR_Y_POS (Color){80, 220, 120, 255}
#define COR_Y_NEG (Color){80, 200, 110, 255}

#define COR_Z_POS (Color){80, 140, 255, 255}
#define COR_Z_NEG (Color){80, 140, 255, 255}

/**
 * Desenha o gizmo direcional na tela
 * @param cam Camera da cena 3D
 * @param largura Largura da tela
 */
void DrawGizmo(Camera cam, int largura);

// Gizmo de movimentação por eixos (mover objeto selecionado)
void UpdateMoveGizmo(Camera cam);
void DrawMoveGizmo(Camera cam);

#endif // GIZMO_H
