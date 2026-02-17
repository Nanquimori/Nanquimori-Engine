#ifndef HELP_PANEL_H
#define HELP_PANEL_H

#include "raylib.h"

// Estado do painel de ajuda
typedef struct
{
    bool mostrar;
    bool bloquearFechar;
} HelpPanel;

// Inicializar painel de ajuda
void InitHelpPanel(void);

// Atualizar estado do painel de ajuda
void UpdateHelpPanel(void);

// Desenhar painel de ajuda
void DrawHelpPanel(void);

// Obter estado de mostrar
bool HelpPanelShouldShow(void);

// Definir se deve mostrar
void SetHelpPanelShow(bool show);

// Fechar painel
void CloseHelpPanel(void);

#endif // HELP_PANEL_H
