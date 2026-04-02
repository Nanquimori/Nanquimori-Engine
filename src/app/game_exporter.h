#ifndef GAME_EXPORTER_H
#define GAME_EXPORTER_H

#include "scene/scene_manager.h"
#include <stddef.h>

bool ExportGameBuild(const ProjectExportSettings *settings, char *status, size_t statusSize);

#endif // GAME_EXPORTER_H
