#include "file_explorer.h"
#include "scene/scene_manager.h"
#include "scene/outliner.h"
#include "ui_button.h"
#include "ui_style.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define ITEM_ALTURA 36
#define BARRA_SUPERIOR 72

// Cores
#define COR_FUNDO (GetUIStyle()->panelBgAlt)
#define COR_PAINEL (GetUIStyle()->panelBg)
#define COR_PAINEL_HOVER (GetUIStyle()->panelBgHover)
#define COR_BORDA (GetUIStyle()->panelBorder)
#define COR_TEXTO (GetUIStyle()->textPrimary)
#define COR_TEXTO_SUAVE (GetUIStyle()->textSecondary)
#define COR_DESTAQUE (GetUIStyle()->accent)

FileExplorer fileExplorer = {0};
static bool pathFeedbackInvalid = false;
static double pathFeedbackUntil = 0.0;

static void EnsureProjectsRootPath(char *out, size_t outSize)
{
    if (outSize == 0)
        return;
    out[0] = '\0';

    const char *cwd = GetWorkingDirectory();
    if (!cwd || cwd[0] == '\0')
        return;

    snprintf(out, outSize, "%s/projects", cwd);
    if (!DirectoryExists(out))
    {
#ifdef _WIN32
        _mkdir(out);
#else
        mkdir(out, 0755);
#endif
    }
}

bool MatchFiltroExtensao(const char *caminho, int filtro)
{
    if (!IsPathFile(caminho))
        return true; // Diretórios sempre passam

    const char *ext = strrchr(caminho, '.');
    if (!ext)
        return false;

    ext++;

    switch (filtro)
    {
    case 1: // .obj
        return strcmp(ext, "obj") == 0 || strcmp(ext, "OBJ") == 0;
    case 2: // .glb
        return strcmp(ext, "glb") == 0 || strcmp(ext, "GLB") == 0;
    case 3: // .gltf
        return strcmp(ext, "gltf") == 0 || strcmp(ext, "GLTF") == 0;
    case 4: // .fbx
        return strcmp(ext, "fbx") == 0 || strcmp(ext, "FBY") == 0;
    default:
        return true;
    }
}

bool MatchBuscaTexto(const char *caminho, const char *texto)
{
    if (texto[0] == '\0')
        return true; // Se não há texto, aceita tudo

    const char *nome = GetFileName(caminho);

    // Busca case-insensitive
    char nomeMinusculo[256] = {0};
    char textoMinusculo[256] = {0};

    TextCopy(nomeMinusculo, nome);
    TextCopy(textoMinusculo, texto);

    // Converter para minúsculas
    for (int i = 0; nomeMinusculo[i]; i++)
        nomeMinusculo[i] = tolower(nomeMinusculo[i]);
    for (int i = 0; textoMinusculo[i]; i++)
        textoMinusculo[i] = tolower(textoMinusculo[i]);

    return strstr(nomeMinusculo, textoMinusculo) != NULL;
}

static void NormalizePathInput(char *path)
{
    if (!path)
        return;

    int len = (int)strlen(path);
    int start = 0;
    while (start < len && isspace((unsigned char)path[start]))
        start++;

    int end = len - 1;
    while (end >= start && isspace((unsigned char)path[end]))
        end--;

    if (start > 0 || end < len - 1)
    {
        int out = 0;
        for (int i = start; i <= end; i++)
            path[out++] = path[i];
        path[out] = '\0';
        len = out;
    }

    if (len >= 2 && path[0] == '"' && path[len - 1] == '"')
    {
        for (int i = 1; i < len - 1; i++)
            path[i - 1] = path[i];
        path[len - 2] = '\0';
    }
}

static bool TrySetCurrentPath(const char *newPath)
{
    if (!newPath || newPath[0] == '\0')
        return false;
    if (!DirectoryExists(newPath))
        return false;

    TextCopy(fileExplorer.caminhoAtual, newPath);
    TextCopy(fileExplorer.bufferCaminho, newPath);
    TextCopy(fileExplorer.bufferTexto, "");
    if (fileExplorer.arquivosCarregados)
        UnloadDirectoryFiles(fileExplorer.arquivos);
    fileExplorer.arquivos = LoadDirectoryFiles(fileExplorer.caminhoAtual);
    fileExplorer.arquivosCarregados = true;
    TextInputInit(&fileExplorer.inputBusca);
    fileExplorer.inputBusca.active = true;
    pathFeedbackInvalid = false;
    pathFeedbackUntil = 0.0;
    return true;
}

void InitFileExplorer(void)
{
    fileExplorer.aberto = false;
    fileExplorer.arquivosCarregados = false;
    fileExplorer.extensaoFiltro = 0;
    fileExplorer.modoEdicao = false;
    fileExplorer.modoEdicaoCaminho = false;
    fileExplorer.mostrarMenuFile = false;
    fileExplorer.mostrarSubmenuImport = false;
    fileExplorer.itemHoverFile = -1;
    fileExplorer.itemHoverImport = -1;
    fileExplorer.menuFileAbertoEsteFrame = false;
    fileExplorer.modoProjetoAbrir = false;
    fileExplorer.modoProjetoSalvar = false;
    TextCopy(fileExplorer.bufferNomeProjeto, "");
    TextCopy(fileExplorer.caminhoAtual, GetWorkingDirectory());
    TextCopy(fileExplorer.bufferCaminho, GetWorkingDirectory());
    TextCopy(fileExplorer.caminhoSelecionado, "");
    TextCopy(fileExplorer.bufferTexto, "");
    pathFeedbackInvalid = false;
    pathFeedbackUntil = 0.0;
    TextInputInit(&fileExplorer.inputNomeProjeto);
    TextInputInit(&fileExplorer.inputCaminho);
    TextInputInit(&fileExplorer.inputBusca);
}

void OpenFileExplorer(int filtro)
{
    fileExplorer.aberto = true;
    fileExplorer.modoEdicao = true;
    fileExplorer.modoEdicaoCaminho = false;
    fileExplorer.extensaoFiltro = filtro;
    TextCopy(fileExplorer.bufferTexto, "");
    TextCopy(fileExplorer.bufferCaminho, fileExplorer.caminhoAtual);
    fileExplorer.modoProjetoAbrir = false;
    fileExplorer.modoProjetoSalvar = false;
    TextInputInit(&fileExplorer.inputCaminho);
    TextInputInit(&fileExplorer.inputBusca);
    fileExplorer.inputBusca.active = true;

    if (!fileExplorer.arquivosCarregados)
    {
        fileExplorer.arquivos = LoadDirectoryFiles(fileExplorer.caminhoAtual);
        fileExplorer.arquivosCarregados = true;
    }
}

void CloseFileExplorer(void)
{
    fileExplorer.aberto = false;
    fileExplorer.modoEdicao = false;
    fileExplorer.modoEdicaoCaminho = false;
    fileExplorer.modoProjetoAbrir = false;
    fileExplorer.modoProjetoSalvar = false;
    TextCopy(fileExplorer.bufferTexto, "");
    TextCopy(fileExplorer.bufferCaminho, fileExplorer.caminhoAtual);
    pathFeedbackInvalid = false;
    pathFeedbackUntil = 0.0;
    TextInputInit(&fileExplorer.inputNomeProjeto);
    TextInputInit(&fileExplorer.inputCaminho);
    TextInputInit(&fileExplorer.inputBusca);
}

void OpenProjectExplorer(void)
{
    fileExplorer.aberto = true;
    fileExplorer.modoEdicao = true;
    fileExplorer.modoEdicaoCaminho = false;
    fileExplorer.extensaoFiltro = 0;
    TextCopy(fileExplorer.bufferTexto, "");
    fileExplorer.modoProjetoAbrir = true;
    fileExplorer.modoProjetoSalvar = false;
    TextInputInit(&fileExplorer.inputCaminho);
    TextInputInit(&fileExplorer.inputBusca);
    fileExplorer.inputBusca.active = true;

    char projectsPath[MAX_FILEPATH_SIZE] = {0};
    EnsureProjectsRootPath(projectsPath, sizeof(projectsPath));
    if (projectsPath[0] != '\0')
        TextCopy(fileExplorer.caminhoAtual, projectsPath);
    TextCopy(fileExplorer.bufferCaminho, fileExplorer.caminhoAtual);
    pathFeedbackInvalid = false;
    pathFeedbackUntil = 0.0;

    if (fileExplorer.arquivosCarregados)
        UnloadDirectoryFiles(fileExplorer.arquivos);
    fileExplorer.arquivos = LoadDirectoryFiles(fileExplorer.caminhoAtual);
    fileExplorer.arquivosCarregados = true;
}

void OpenProjectSaveAs(void)
{
    fileExplorer.aberto = true;
    fileExplorer.modoEdicao = false;
    fileExplorer.modoEdicaoCaminho = false;
    fileExplorer.modoProjetoAbrir = false;
    fileExplorer.modoProjetoSalvar = true;
    TextCopy(fileExplorer.bufferNomeProjeto, "");
    TextInputInit(&fileExplorer.inputNomeProjeto);
    fileExplorer.inputNomeProjeto.active = true;
}

void UpdateFileExplorer(void)
{
    if (!fileExplorer.aberto)
        return;

    if (fileExplorer.modoProjetoSalvar)
    {
        // Modal simples de Save As
        Vector2 mouse = GetMousePosition();
        int larguraTela = GetScreenWidth();
        int alturaTela = GetScreenHeight();
        Rectangle painel = {(float)(larguraTela / 2 - 200), (float)(alturaTela / 2 - 90), 400, 180};
        Rectangle btnSalvar = {painel.x + 20, painel.y + 110, 120, 28};
        Rectangle btnCancelar = {painel.x + 160, painel.y + 110, 120, 28};

        if (!CheckCollisionPointRec(mouse, painel) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            CloseFileExplorer();
            return;
        }

        UIButtonState saveState = UIButtonGetState(btnSalvar);
        UIButtonState cancelState = UIButtonGetState(btnCancelar);

        if (saveState.clicked)
        {
            if (fileExplorer.bufferNomeProjeto[0] != '\0')
                SaveProjectAs(fileExplorer.bufferNomeProjeto);
            CloseFileExplorer();
            return;
        }

        if (cancelState.clicked)
        {
            CloseFileExplorer();
            return;
        }

        return;
    }

    Vector2 mouse = GetMousePosition();
    int larguraTela = GetScreenWidth();
    int alturaTela = GetScreenHeight();

    // Dimensões do painel (deve ser igual ao DrawFileExplorer)
    int painelLargura = 700;
    int painelAltura = 500;
    int painelX = (larguraTela - painelLargura) / 2;
    int painelY = (alturaTela - painelAltura) / 2;

    // Área do painel completo
    Rectangle areaPainel = {
        (float)painelX,
        (float)painelY,
        (float)painelLargura,
        (float)painelAltura};

    // Área do campo de caminho (na barra superior)
    Rectangle campoCaminho = {
        (float)(painelX + 74),
        (float)(painelY + 20),
        (float)(painelLargura - 128),
        24.0f};

    // Área do campo de texto de busca
    Rectangle campoTexto = {
        (float)(painelX + 16),
        (float)(painelY + BARRA_SUPERIOR + 8),
        (float)(painelLargura - 56),
        24.0f};

    // Fechar ao clicar fora do painel
    if (!CheckCollisionPointRec(mouse, areaPainel) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        CloseFileExplorer();
        return;
    }

    (void)campoCaminho;
    (void)campoTexto;

    // Botão voltar - DENTRO DO PAINEL
    Rectangle btnVoltar = {(float)(painelX + painelLargura - 44), (float)(painelY + 20), 24.0f, 24.0f};
    UIButtonState backState = UIButtonGetState(btnVoltar);
    if (backState.clicked)
    {
        char caminhoAnterior[MAX_FILEPATH_SIZE] = {0};
        TextCopy(caminhoAnterior, GetPrevDirectoryPath(fileExplorer.caminhoAtual));
        TrySetCurrentPath(caminhoAnterior);
        return;
    }

    // Navegação de arquivos
    int inicioY = painelY + BARRA_SUPERIOR + 40;
    int contadorItems = 0;

    for (int i = 0; i < (int)fileExplorer.arquivos.count; i++)
    {
        if (!MatchFiltroExtensao(fileExplorer.arquivos.paths[i], fileExplorer.extensaoFiltro))
            continue;

        if (!MatchBuscaTexto(fileExplorer.arquivos.paths[i], fileExplorer.bufferTexto))
            continue;

        Rectangle areaItem = {
            (float)(painelX + 8),
            (float)(inicioY + ITEM_ALTURA * contadorItems),
            (float)(painelLargura - 16),
            (float)(ITEM_ALTURA - 4)};

        // Não processar se está fora da área do painel
        if (areaItem.y + areaItem.height > painelY + painelAltura)
            break;

        bool hover = CheckCollisionPointRec(mouse, areaItem);

        // Verificar se é diretório
        bool ehDiretorio = !IsPathFile(fileExplorer.arquivos.paths[i]);
        bool diretorioExiste = DirectoryExists(fileExplorer.arquivos.paths[i]);

        if (ehDiretorio && diretorioExiste)
        {
            if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            {
                if (fileExplorer.modoProjetoAbrir)
                {
                    char projectJson[MAX_FILEPATH_SIZE];
                    snprintf(projectJson, sizeof(projectJson), "%s/project.json", fileExplorer.arquivos.paths[i]);
                    if (FileExists(projectJson))
                    {
                        OpenProject(projectJson);
                        CloseFileExplorer();
                        return;
                    }
                }

                TrySetCurrentPath(fileExplorer.arquivos.paths[i]);
                return;
            }
        }
        else if (!ehDiretorio)
        {
            if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            {
                TextCopy(fileExplorer.caminhoSelecionado, fileExplorer.arquivos.paths[i]);
                if (fileExplorer.modoProjetoAbrir)
                {
                    const char *nome = GetFileName(fileExplorer.caminhoSelecionado);
                    if (nome && strcmp(nome, "project.json") == 0)
                    {
                        OpenProject(fileExplorer.caminhoSelecionado);
                        CloseFileExplorer();
                        return;
                    }
                }
                else
                {
                    CloseFileExplorer();
                    return;
                }
            }
        }

        contadorItems++;
    }
}

void DrawFileExplorer(void)
{
    if (!fileExplorer.aberto)
        return;

    if (fileExplorer.modoProjetoSalvar)
    {
        int larguraTela = GetScreenWidth();
        int alturaTela = GetScreenHeight();

        DrawRectangle(0, 0, larguraTela, alturaTela, GetUIStyle()->panelOverlay);

        Rectangle painel = {(float)(larguraTela / 2 - 200), (float)(alturaTela / 2 - 90), 400, 180};
        DrawRectangleRec(painel, COR_PAINEL);
        DrawRectangle((int)painel.x + 1, (int)painel.y + 1, 4, (int)painel.height - 2, GetUIStyle()->accent);

        DrawText("Save Project As", (int)painel.x + 24, (int)painel.y + 20, 14, COR_TEXTO);
        DrawText("Nome do projeto:", (int)painel.x + 20, (int)painel.y + 45, 10, COR_TEXTO_SUAVE);

        TextInputConfig cfg = {0};
        cfg.fontSize = 12;
        cfg.padding = 6;
        cfg.textColor = COR_TEXTO;
        cfg.bgColor = COR_FUNDO;
        cfg.borderColor = COR_BORDA;
        cfg.selectionColor = GetUIStyle()->inputSelection;
        cfg.caretColor = GetUIStyle()->caret;
        cfg.filter = TEXT_INPUT_FILTER_NONE;
        cfg.allowInput = true;

        Rectangle nomeBox = {painel.x + 20, painel.y + 60, painel.width - 40, 28};
        int flags = TextInputDraw(nomeBox, fileExplorer.bufferNomeProjeto,
                                  (int)sizeof(fileExplorer.bufferNomeProjeto),
                                  &fileExplorer.inputNomeProjeto, &cfg);
        if ((flags & TEXT_INPUT_SUBMITTED) && fileExplorer.bufferNomeProjeto[0] != '\0')
        {
            SaveProjectAs(fileExplorer.bufferNomeProjeto);
            CloseFileExplorer();
            return;
        }

        Rectangle btnSalvar = {painel.x + 20, painel.y + 110, 120, 28};
        Rectangle btnCancelar = {painel.x + 160, painel.y + 110, 120, 28};

        Vector2 mouse = GetMousePosition();
        bool hoverSalvar = CheckCollisionPointRec(mouse, btnSalvar);
        bool hoverCancelar = CheckCollisionPointRec(mouse, btnCancelar);

        const UIStyle *style = GetUIStyle();
        UIButtonConfig bwCfg = {0};
        bwCfg.centerText = true;
        bwCfg.fontSize = 12;
        bwCfg.padding = 6;
        bwCfg.textColor = style->buttonText;
        bwCfg.textHoverColor = style->buttonTextHover;
        bwCfg.bgColor = style->buttonBg;
        bwCfg.bgHoverColor = style->buttonBgHover;
        bwCfg.borderColor = style->buttonBorder;
        bwCfg.borderHoverColor = style->buttonBorder;
        bwCfg.borderThickness = 1.0f;

        UIButtonDraw(btnSalvar, "Salvar", nullptr, &bwCfg, hoverSalvar);
        UIButtonDraw(btnCancelar, "Cancelar", nullptr, &bwCfg, hoverCancelar);
        return;
    }

    int larguraTela = GetScreenWidth();
    int alturaTela = GetScreenHeight();

    // Overlay escuro
    DrawRectangle(0, 0, larguraTela, alturaTela, GetUIStyle()->panelOverlay);

    // Painel do explorador
    int painelLargura = 700;
    int painelAltura = 500;
    int painelX = (larguraTela - painelLargura) / 2;
    int painelY = (alturaTela - painelAltura) / 2;

    DrawRectangle(painelX, painelY, painelLargura, painelAltura, COR_PAINEL);
    DrawRectangle(painelX + 1, painelY + 1, 4, painelAltura - 2, GetUIStyle()->accent);

    // Barra superior do explorador
    DrawRectangle(painelX, painelY, painelLargura, BARRA_SUPERIOR, GetUIStyle()->panelBgAlt);
    DrawText(fileExplorer.modoProjetoAbrir ? "Open Project" : "Import Model", painelX + 16, painelY + 5, 13, GetUIStyle()->accent);
    DrawText("PATH", painelX + 16, painelY + 24, 10, COR_TEXTO_SUAVE);

    TextInputConfig cfg = {0};
    cfg.fontSize = 12;
    cfg.padding = 6;
    cfg.textColor = COR_TEXTO;
    cfg.bgColor = GetUIStyle()->inputBg;
    cfg.borderColor = (pathFeedbackInvalid && GetTime() < pathFeedbackUntil) ? (Color){150, 46, 42, 255} : COR_BORDA;
    cfg.selectionColor = GetUIStyle()->inputSelection;
    cfg.caretColor = GetUIStyle()->caret;
    cfg.filter = TEXT_INPUT_FILTER_NONE;
    cfg.allowInput = true;

    Rectangle caminhoBox = {(float)(painelX + 74), (float)(painelY + 20), (float)(painelLargura - 128), 24.0f};
    int pathFlags = TextInputDraw(caminhoBox, fileExplorer.bufferCaminho,
                                  MAX_FILEPATH_SIZE, &fileExplorer.inputCaminho, &cfg);
    if (pathFlags & TEXT_INPUT_SUBMITTED)
    {
        char pathParsed[MAX_FILEPATH_SIZE] = {0};
        TextCopy(pathParsed, fileExplorer.bufferCaminho);
        NormalizePathInput(pathParsed);
        TextCopy(fileExplorer.bufferCaminho, pathParsed);
        if (!TrySetCurrentPath(pathParsed))
        {
            pathFeedbackInvalid = true;
            pathFeedbackUntil = GetTime() + 2.0;
            fileExplorer.inputCaminho.active = true;
        }
    }
    if (pathFeedbackInvalid && GetTime() >= pathFeedbackUntil)
        pathFeedbackInvalid = false;

    // Botão voltar
    Vector2 mouse = GetMousePosition();
    Rectangle btnVoltar = {(float)(painelX + painelLargura - 44), (float)(painelY + 20), 24.0f, 24.0f};
    bool hoverVoltar = CheckCollisionPointRec(mouse, btnVoltar);

    const UIStyle *style = GetUIStyle();
    UIButtonConfig backCfg = {0};
    backCfg.centerText = true;
    backCfg.fontSize = 12;
    backCfg.padding = 4;
    backCfg.textColor = style->buttonText;
    backCfg.textHoverColor = style->buttonTextHover;
    backCfg.bgColor = style->buttonBg;
    backCfg.bgHoverColor = style->buttonBgHover;
    backCfg.borderColor = style->buttonBorder;
    backCfg.borderHoverColor = style->buttonBorder;
    backCfg.borderThickness = 1.0f;
    UIButtonDraw(btnVoltar, "<", nullptr, &backCfg, hoverVoltar);

    if (pathFeedbackInvalid)
        DrawText("PATH invalido ou inexistente", painelX + 74, painelY + 48, 10, (Color){186, 56, 52, 255});

    // Campo de texto de busca
    Rectangle buscaBox = {(float)(painelX + 16), (float)(painelY + BARRA_SUPERIOR + 8), (float)(painelLargura - 56), 24.0f};
    TextInputDraw(buscaBox, fileExplorer.bufferTexto,
                  MAX_BUFFER_TEXTO, &fileExplorer.inputBusca, &cfg);

    // Label para o campo de busca
    DrawText("BUSCAR", painelX + 16, painelY + BARRA_SUPERIOR - 8, 10, COR_TEXTO_SUAVE);

    // Lista de arquivos dentro do painel
    int inicioY = painelY + BARRA_SUPERIOR + 40;
    int contadorItems = 0;

    for (int i = 0; i < (int)fileExplorer.arquivos.count; i++)
    {
        if (!MatchFiltroExtensao(fileExplorer.arquivos.paths[i], fileExplorer.extensaoFiltro))
            continue;

        if (!MatchBuscaTexto(fileExplorer.arquivos.paths[i], fileExplorer.bufferTexto))
            continue;

        Rectangle areaItem = {
            (float)(painelX + 8),
            (float)(inicioY + ITEM_ALTURA * contadorItems),
            (float)(painelLargura - 16),
            (float)(ITEM_ALTURA - 4)};

        // Não desenhar se está fora da área do painel
        if (areaItem.y + areaItem.height > painelY + painelAltura)
            break;

        bool hover = CheckCollisionPointRec(mouse, areaItem);
        const UIStyle *style = GetUIStyle();
        Color fundoItem = hover ? style->accentSoft : style->itemBg;

        DrawRectangleRec(areaItem, fundoItem);
        if (!hover)
            DrawLine((int)areaItem.x + 6, (int)(areaItem.y + areaItem.height), (int)(areaItem.x + areaItem.width - 6), (int)(areaItem.y + areaItem.height), style->panelBorderSoft);

        const char *nome = GetFileName(fileExplorer.arquivos.paths[i]);
        Color corTexto = hover ? style->buttonTextHover : COR_TEXTO;

        if (!IsPathFile(fileExplorer.arquivos.paths[i]) && DirectoryExists(fileExplorer.arquivos.paths[i]))
        {
            if (!hover)
                corTexto = COR_DESTAQUE;
            DrawRectangle((int)areaItem.x + 4, (int)areaItem.y + 6, 3, (int)areaItem.height - 12, hover ? style->accent : style->accentSoft);
        }

        DrawText(nome, (int)(areaItem.x + 12), (int)(areaItem.y + 10), 12, corTexto);

        contadorItems++;
    }
}

bool FileExplorerArquivoSelecionado(char *saida)
{
    if (fileExplorer.caminhoSelecionado[0] != '\0')
    {
        TextCopy(saida, fileExplorer.caminhoSelecionado);
        TextCopy(fileExplorer.caminhoSelecionado, "");
        return true;
    }
    return false;
}

void UnloadFileExplorer(void)
{
    if (fileExplorer.arquivosCarregados)
    {
        UnloadDirectoryFiles(fileExplorer.arquivos);
        fileExplorer.arquivosCarregados = false;
    }
}

void ToggleFileMenu(void)
{
    fileExplorer.mostrarMenuFile = !fileExplorer.mostrarMenuFile;
    fileExplorer.menuFileAbertoEsteFrame = fileExplorer.mostrarMenuFile;
}

bool IsFileMenuOpen(void)
{
    return fileExplorer.mostrarMenuFile;
}

void UpdateFileMenu(void)
{
    if (!fileExplorer.mostrarMenuFile)
        return;

    Vector2 mouse = GetMousePosition();

    // Menu principal File
    const float menuX = (float)PAINEL_LARGURA + 8.0f;
    const float menuY = 24.0f;
    Rectangle menuFileRect = {menuX, menuY, 200.0f, 72.0f};

    // Itens do menu
    Rectangle itemImport = {menuFileRect.x, menuFileRect.y, menuFileRect.width, 24.0f};
    Rectangle itemOpen = {menuFileRect.x, menuFileRect.y + 24.0f, menuFileRect.width, 24.0f};
    Rectangle itemSave = {menuFileRect.x, menuFileRect.y + 48.0f, menuFileRect.width, 24.0f};

    // Área do submenu
    const float submenuItemH = 24.0f;
    const float submenuCount = 4.0f;
    Rectangle submenuRect = {menuFileRect.x + menuFileRect.width + 2, menuFileRect.y, 150.0f, submenuItemH * submenuCount};
    bool mouseEmSubmenu = CheckCollisionPointRec(mouse, submenuRect);

    // Verificar hover no menu principal
    fileExplorer.itemHoverFile = -1;
    if (CheckCollisionPointRec(mouse, itemImport) || mouseEmSubmenu)
    {
        fileExplorer.itemHoverFile = 0;
        fileExplorer.mostrarSubmenuImport = true;
    }
    else if (CheckCollisionPointRec(mouse, itemOpen))
    {
        fileExplorer.itemHoverFile = 1;
        fileExplorer.mostrarSubmenuImport = false;
    }
    else if (CheckCollisionPointRec(mouse, itemSave))
    {
        fileExplorer.itemHoverFile = 2;
        fileExplorer.mostrarSubmenuImport = false;
    }
    else if (!CheckCollisionPointRec(mouse, menuFileRect))
    {
        fileExplorer.mostrarSubmenuImport = false;
    }

    // Submenu
    if (fileExplorer.mostrarSubmenuImport && fileExplorer.itemHoverFile == 0)
    {
        Rectangle itemObj = {submenuRect.x, submenuRect.y, submenuRect.width, 24.0f};
        Rectangle itemGlb = {submenuRect.x, submenuRect.y + 24.0f, submenuRect.width, 24.0f};
        Rectangle itemGltf = {submenuRect.x, submenuRect.y + 48.0f, submenuRect.width, 24.0f};
        Rectangle itemFbx = {submenuRect.x, submenuRect.y + 72.0f, submenuRect.width, 24.0f};

        fileExplorer.itemHoverImport = -1;
        if (CheckCollisionPointRec(mouse, itemObj))
            fileExplorer.itemHoverImport = 0;
        else if (CheckCollisionPointRec(mouse, itemGlb))
            fileExplorer.itemHoverImport = 1;
        else if (CheckCollisionPointRec(mouse, itemGltf))
            fileExplorer.itemHoverImport = 2;
        else if (CheckCollisionPointRec(mouse, itemFbx))
            fileExplorer.itemHoverImport = 3;

        // Processar cliques no submenu
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !fileExplorer.menuFileAbertoEsteFrame)
        {
            if (fileExplorer.itemHoverImport >= 0 && fileExplorer.itemHoverImport <= 3)
            {
                OpenFileExplorer(fileExplorer.itemHoverImport + 1);
                fileExplorer.mostrarMenuFile = false;
                fileExplorer.mostrarSubmenuImport = false;
            }
        }
    }

    // Processar cliques no menu principal
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !fileExplorer.menuFileAbertoEsteFrame)
    {
        if (fileExplorer.itemHoverFile == 1)
        {
            OpenProjectExplorer();
            fileExplorer.mostrarMenuFile = false;
        }
        else if (fileExplorer.itemHoverFile == 2)
        {
            if (!SaveProject())
                OpenProjectSaveAs();
            fileExplorer.mostrarMenuFile = false;
        }
        else if (!CheckCollisionPointRec(mouse, menuFileRect))
        {
            fileExplorer.mostrarMenuFile = false;
            fileExplorer.mostrarSubmenuImport = false;
        }
    }

    fileExplorer.menuFileAbertoEsteFrame = false;
}

void DrawFileMenu(void)
{
    if (!fileExplorer.mostrarMenuFile)
        return;

    const float menuX = (float)PAINEL_LARGURA + 8.0f;
    const float menuY = 24.0f;
    const UIStyle *style = GetUIStyle();

    // Menu principal File
    Rectangle menuFileRect = {menuX, menuY, 200.0f, 72.0f};
    DrawRectangleRec(menuFileRect, style->panelBg);

    // Itens do menu
    Rectangle itemImport = {menuFileRect.x, menuFileRect.y, menuFileRect.width, 24.0f};
    Rectangle itemOpen = {menuFileRect.x, menuFileRect.y + 24.0f, menuFileRect.width, 24.0f};
    Rectangle itemSave = {menuFileRect.x, menuFileRect.y + 48.0f, menuFileRect.width, 24.0f};

    // Área do submenu
    const float submenuItemH = 24.0f;
    const float submenuCount = 4.0f;
    Rectangle submenuRect = {menuFileRect.x + menuFileRect.width + 2, menuFileRect.y, 150.0f, submenuItemH * submenuCount};

    // Desenhar itens
    bool hoverImport = fileExplorer.itemHoverFile == 0;
    bool hoverOpen = fileExplorer.itemHoverFile == 1;
    bool hoverSave = fileExplorer.itemHoverFile == 2;

    DrawRectangleRec(itemImport, hoverImport ? style->buttonBgHover : style->buttonBg);
    DrawRectangleRec(itemOpen, hoverOpen ? style->buttonBgHover : style->buttonBg);
    DrawRectangleRec(itemSave, hoverSave ? style->buttonBgHover : style->buttonBg);

    DrawText("Import Model", (int)(itemImport.x + 10), (int)(itemImport.y + 6), 12, hoverImport ? style->buttonTextHover : style->buttonText);
    DrawText("Open Project", (int)(itemOpen.x + 10), (int)(itemOpen.y + 6), 12, hoverOpen ? style->buttonTextHover : style->buttonText);
    DrawText("Save Project", (int)(itemSave.x + 10), (int)(itemSave.y + 6), 12, hoverSave ? style->buttonTextHover : style->buttonText);

    // Seta para indicar submenu
    DrawText(">", (int)(itemImport.x + menuFileRect.width - 20), (int)(itemImport.y + 6), 12, hoverImport ? style->buttonTextHover : style->buttonText);

    // SUBMENU DE IMPORT
    if (fileExplorer.mostrarSubmenuImport && fileExplorer.itemHoverFile == 0)
    {
        DrawRectangleRec(submenuRect, style->buttonBg);

        Rectangle itemObj = {submenuRect.x, submenuRect.y, submenuRect.width, submenuItemH};
        Rectangle itemGlb = {submenuRect.x, submenuRect.y + submenuItemH, submenuRect.width, submenuItemH};
        Rectangle itemGltf = {submenuRect.x, submenuRect.y + submenuItemH * 2.0f, submenuRect.width, submenuItemH};
        Rectangle itemFbx = {submenuRect.x, submenuRect.y + submenuItemH * 3.0f, submenuRect.width, submenuItemH};

        bool hoverObj = fileExplorer.itemHoverImport == 0;
        bool hoverGlb = fileExplorer.itemHoverImport == 1;
        bool hoverGltf = fileExplorer.itemHoverImport == 2;
        bool hoverFbx = fileExplorer.itemHoverImport == 3;

        DrawRectangleRec(itemObj, hoverObj ? style->buttonBgHover : style->buttonBg);
        DrawRectangleRec(itemGlb, hoverGlb ? style->buttonBgHover : style->buttonBg);
        DrawRectangleRec(itemGltf, hoverGltf ? style->buttonBgHover : style->buttonBg);
        DrawRectangleRec(itemFbx, hoverFbx ? style->buttonBgHover : style->buttonBg);

        DrawText(".OBJ (Wavefront)", (int)(itemObj.x + 10), (int)(itemObj.y + 6), 11, hoverObj ? style->buttonTextHover : style->buttonText);
        DrawText(".GLB (glTF Binary)", (int)(itemGlb.x + 10), (int)(itemGlb.y + 6), 11, hoverGlb ? style->buttonTextHover : style->buttonText);
        DrawText(".GLTF (glTF Text)", (int)(itemGltf.x + 10), (int)(itemGltf.y + 6), 11, hoverGltf ? style->buttonTextHover : style->buttonText);
        DrawText(".FBX (Autodesk)", (int)(itemFbx.x + 10), (int)(itemFbx.y + 6), 11, hoverFbx ? style->buttonTextHover : style->buttonText);
    }
}




