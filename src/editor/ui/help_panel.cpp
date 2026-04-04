#include "help_panel.h"
#include "ui_style.h"

static HelpPanel helpPanel = {0};

void InitHelpPanel(void)
{
    helpPanel.mostrar = false;
    helpPanel.bloquearFechar = false;
}

void UpdateHelpPanel(void)
{
    // Nada a atualizar por enquanto
}

void DrawHelpPanel(void)
{
    if (!helpPanel.mostrar)
        return;

    const UIStyle *style = GetUIStyle();

    float W = (float)GetScreenWidth();
    float H = (float)GetScreenHeight();

    float helpX = W / 2.0f - 200.0f;
    float helpY = H / 2.0f - 150.0f;
    float helpW = 400.0f;
    float helpH = 390.0f;

    // Fechar ao clicar fora do painel
    Rectangle areaHelp = {helpX, helpY, helpW, helpH};
    Vector2 mouse = GetMousePosition();

    if (helpPanel.bloquearFechar)
    {
        helpPanel.bloquearFechar = false;
    }
    else if (!CheckCollisionPointRec(mouse, areaHelp) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        helpPanel.mostrar = false;
    }

    DrawRectangle((int)helpX, (int)helpY, (int)helpW, (int)helpH, style->panelOverlay);
    DrawRectangleLinesEx((Rectangle){helpX, helpY, helpW, helpH}, 2, style->panelBorder);

    DrawText("CONTROLES", (int)(helpX + 15), (int)(helpY + 15), 18, style->textPrimary);
    DrawLine((int)helpX, (int)(helpY + 40), (int)(helpX + helpW), (int)(helpY + 40), style->panelBorder);

    DrawText("Mouse Botao Meio (MMB):", (int)(helpX + 15), (int)(helpY + 50), 12, style->textSecondary);
    DrawText("Rotacionar a camera", (int)(helpX + 30), (int)(helpY + 65), 11, style->textPrimary);

    DrawText("Shift + Mouse Botao Meio:", (int)(helpX + 15), (int)(helpY + 85), 12, style->textSecondary);
    DrawText("Pan (mover) a camera", (int)(helpX + 30), (int)(helpY + 100), 11, style->textPrimary);

    DrawText("Scroll (Roda do mouse):", (int)(helpX + 15), (int)(helpY + 120), 12, style->textSecondary);
    DrawText("Zoom (aproximar/afastar)", (int)(helpX + 30), (int)(helpY + 135), 11, style->textPrimary);

    DrawText("Mouse Botao Direito (RMB):", (int)(helpX + 15), (int)(helpY + 155), 12, style->textSecondary);
    DrawText("Orbita ao redor do alvo", (int)(helpX + 30), (int)(helpY + 170), 11, style->textPrimary);

    DrawText("Ctrl + Z:", (int)(helpX + 15), (int)(helpY + 190), 12, style->textSecondary);
    DrawText("Desfazer (outliner, properties e gizmo)", (int)(helpX + 30), (int)(helpY + 205), 11, style->textPrimary);

    DrawText("Ctrl + Y:", (int)(helpX + 15), (int)(helpY + 220), 12, style->textSecondary);
    DrawText("Refazer (properties e gizmo)", (int)(helpX + 30), (int)(helpY + 235), 11, style->textPrimary);

    DrawText("Gizmo de mover (eixos X/Y/Z):", (int)(helpX + 15), (int)(helpY + 255), 12, style->textSecondary);
    DrawText("W = mover / E = escala / R = rotacionar", (int)(helpX + 30), (int)(helpY + 270), 11, style->textPrimary);
    DrawText("Clique e arraste no eixo", (int)(helpX + 30), (int)(helpY + 285), 11, style->textPrimary);
    DrawText("Ctrl + arraste: snap", (int)(helpX + 30), (int)(helpY + 300), 11, style->textPrimary);
    DrawText("Shift + arraste: lento/suave", (int)(helpX + 30), (int)(helpY + 315), 11, style->textPrimary);

    DrawText("Painel Info (RAY):", (int)(helpX + 15), (int)(helpY + 335), 12, style->textSecondary);
    DrawText("Botoes 2D/3D ligam o ray", (int)(helpX + 30), (int)(helpY + 350), 11, style->textPrimary);
}

bool HelpPanelShouldShow(void)
{
    return helpPanel.mostrar;
}

void SetHelpPanelShow(bool show)
{
    helpPanel.mostrar = show;
    if (show)
        helpPanel.bloquearFechar = true;
}

void CloseHelpPanel(void)
{
    helpPanel.mostrar = false;
}
