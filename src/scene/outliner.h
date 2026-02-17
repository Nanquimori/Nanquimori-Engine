#ifndef OUTLINER_H
#define OUTLINER_H

#include "raylib.h"

#define PAINEL_LARGURA 280
#define MAX_OBJETOS 64
#define MAX_UNDO 32
#define MAX_NOME 31
#define MAX_PROTO_CUSTOM 16

typedef struct
{
    char name[32];
    Color base;
    Color secondary;
} ProtoCustomEntry;

typedef struct
{
    int id;
    char nome[32];
    Vector3 posicao;
    Vector3 rotacao;
    int paiId;
    bool ativo;
    bool selecionado;
    char caminhoModelo[256]; // Para armazenar filepath do modelo importado
    bool protoEnabled;
    Color protoBaseColor;
    Color protoSecondaryColor;
    int protoPack;
    char protoCustomName[32];
    Color protoCustomBase;
    Color protoCustomSecondary;
    int protoCustomCount;
    ProtoCustomEntry protoCustomEntries[MAX_PROTO_CUSTOM];
    // Física / colisão
    bool physStatic;
    bool physRigidbody;
    bool physCollider;
    bool physGravity;
    bool physTerrain;
    float physMass;
    int physShape;
    Vector3 physSize;
} ObjetoCena;

// Funções públicas
void InitOutliner(void);
int RegistrarObjeto(const char *nome, Vector3 posicao, int paiId);
void RemoverObjetoRecursivoId(int id);
void DrawOutliner(void);
void ProcessarOutliner(void);
int BuscarIndicePorId(int id);
void Undo(void);
const char *ObterNomeObjeto(int id);
bool ObjetoExisteNoOutliner(const char *nome);
int ObterObjetoSelecionadoId(void);
void SelecionarObjetoPorId(int id);
int ObterProximoId(void);
void DefinirProximoId(int id);

// Acesso aos objetos
extern ObjetoCena objetos[MAX_OBJETOS];
extern int totalObjetos;

#endif // OUTLINER_H
