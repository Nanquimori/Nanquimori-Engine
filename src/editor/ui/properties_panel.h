#ifndef PROPERTIES_PANEL_H
#define PROPERTIES_PANEL_H

#include "raylib.h"
#include "editor/ui/editor_layout.h"
#include "scene/outliner.h"

// Largura do painel (igual ao outliner)
#define PROPERTIES_PAINEL_LARGURA (GetEditorRightPanelWidth())

typedef enum
{
    COLLISION_SHAPE_MESH_BOUNDS = 0
} CollisionShapeType;

// Inicializar painel de properties
void InitPropertiesPanel(void);

// Desenhar painel de properties
void DrawPropertiesPanel(void);

// Estado de fisica por objeto (por ID)
bool PropertiesIsStatic(int objetoId);
bool PropertiesIsRigidbody(int objetoId);
bool PropertiesHasGravity(int objetoId);
bool PropertiesHasCollider(int objetoId);
bool PropertiesIsTerrain(int objetoId);
bool PropertiesShowCollisions(void);
void SetPropertiesShowCollisions(bool enabled);
int PropertiesGetCollisionShape(int objetoId);
Vector3 PropertiesGetCollisionSize(int objetoId);
float PropertiesGetMass(int objetoId);
void PropertiesSyncToObjeto(ObjetoCena *o);
void PropertiesSyncFromObjeto(const ObjetoCena *o);
bool PropertiesIsBlockingViewport(void);
bool PropertiesHandleTransformShortcuts(void);

#endif // PROPERTIES_PANEL_H
