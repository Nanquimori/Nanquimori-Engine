#ifndef SVG_ASSET_LOADER_H
#define SVG_ASSET_LOADER_H

#include "raylib.h"
#include <stddef.h>

bool ResolveSvgAssetPath(const char *relativePath, char *out, size_t outSize);
Image LoadSvgImageAsset(const char *relativePath, int rasterSize);
Texture2D LoadSvgTextureAsset(const char *relativePath, int rasterSize);

#endif // SVG_ASSET_LOADER_H
