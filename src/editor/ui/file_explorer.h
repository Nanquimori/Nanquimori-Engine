#ifndef FILE_EXPLORER_H
#define FILE_EXPLORER_H

#include "raylib.h"
#include "editor/ui/text_input.h"

#define MAX_FILEPATH_SIZE 1024
#define MAX_BUFFER_TEXTO 256

typedef enum
{
    FILE_EXPLORER_PICK_NONE = 0,
    FILE_EXPLORER_PICK_MODEL_IMPORT,
    FILE_EXPLORER_PICK_EXPORT_ICON
} FileExplorerPickPurpose;

typedef enum
{
    FILE_EXPLORER_VIEW_LIST = 0,
    FILE_EXPLORER_VIEW_THUMBNAILS
} FileExplorerViewMode;

typedef struct
{
    bool aberto;
    char caminhoAtual[MAX_FILEPATH_SIZE];
    char caminhoSelecionado[MAX_FILEPATH_SIZE];
    char bufferTexto[MAX_BUFFER_TEXTO];
    char bufferCaminho[MAX_FILEPATH_SIZE];
    bool modoEdicaoCaminho;
    bool modoEdicao;
    FilePathList arquivos;
    bool arquivosCarregados;
    int extensaoFiltro; // 0: todos, 1: .obj, 2: .glb, 3: .gltf, 4: .fbx
    bool mostrarMenuFile;
    bool mostrarSubmenuImport;
    int itemHoverFile;
    int itemHoverImport;
    bool menuFileAbertoEsteFrame;
    FileExplorerViewMode viewMode;
    float scrollOffset;
    FileExplorerPickPurpose pickPurpose;
    FileExplorerPickPurpose selectedPurpose;
    bool modoProjetoAbrir;
    bool modoProjetoSalvar;
    char bufferNomeProjeto[64];
    TextInputState inputNomeProjeto;
    TextInputState inputCaminho;
    TextInputState inputBusca;
} FileExplorer;

extern FileExplorer fileExplorer;

void InitFileExplorer(void);
void OpenFileExplorer(int filtro);
void CloseFileExplorer(void);
void UpdateFileExplorer(void);
void DrawFileExplorer(void);
void UpdateFileMenu(void);
void DrawFileMenu(void);
bool FileExplorerArquivoSelecionado(char *saida);
void OpenProjectExplorer(void);
void OpenProjectSaveAs(void);
void OpenExportIconExplorer(void);
void UnloadFileExplorer(void);
void ToggleFileMenu(void);
bool IsFileMenuOpen(void);
bool FileExplorerConsumeSelectedExportIconPath(char *saida);

#endif // FILE_EXPLORER_H
