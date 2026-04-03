#include "game_exporter.h"
#include "exe_icon_win32.h"

#include "raylib.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <limits.h>
#endif

static void CopyStringSafe(char *dst, size_t dstSize, const char *src)
{
    if (!dst || dstSize == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static void NormalizePathSeparators(char *path)
{
    if (!path)
        return;
#ifdef _WIN32
    for (size_t i = 0; path[i] != '\0'; i++)
        if (path[i] == '/')
            path[i] = '\\';
#else
    for (size_t i = 0; path[i] != '\0'; i++)
        if (path[i] == '\\')
            path[i] = '/';
#endif
}

static bool IsPathAbsoluteLocal(const char *path)
{
    if (!path || path[0] == '\0')
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return path[1] == ':' && (path[2] == '\\' || path[2] == '/');
}

static void GetDirectoryFromPathLocal(const char *path, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return;
    out[0] = '\0';
    if (!path || path[0] == '\0')
        return;

    const char *lastSlash = strrchr(path, '/');
    const char *lastBack = strrchr(path, '\\');
    const char *last = lastSlash;
    if (lastBack && (!last || lastBack > last))
        last = lastBack;

    if (!last)
    {
        CopyStringSafe(out, outSize, ".");
        return;
    }

    size_t len = (size_t)(last - path);
    if (len >= outSize)
        len = outSize - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

static void TrimTrailingSeparator(char *path)
{
    if (!path || path[0] == '\0')
        return;

    size_t len = strlen(path);
    while (len > 0)
    {
        char c = path[len - 1];
        bool isSeparator = (c == '/' || c == '\\');
        bool isDriveRoot = (len == 3 && path[1] == ':');
        if (!isSeparator || isDriveRoot)
            break;
        path[len - 1] = '\0';
        len--;
    }
}

static void JoinPath(char *out, size_t outSize, const char *left, const char *right)
{
    if (!out || outSize == 0)
        return;
    out[0] = '\0';

    if (!left || left[0] == '\0')
    {
        CopyStringSafe(out, outSize, right);
        NormalizePathSeparators(out);
        return;
    }
    if (!right || right[0] == '\0')
    {
        CopyStringSafe(out, outSize, left);
        NormalizePathSeparators(out);
        return;
    }

    snprintf(out, outSize, "%s/%s", left, right);
    out[outSize - 1] = '\0';
    NormalizePathSeparators(out);
}

static bool NormalizeAbsolutePath(const char *input, char *out, size_t outSize)
{
    if (!input || !out || outSize == 0)
        return false;

    char candidate[1024] = {0};
    if (IsPathAbsoluteLocal(input))
    {
        CopyStringSafe(candidate, sizeof(candidate), input);
    }
    else
    {
        const char *cwd = GetWorkingDirectory();
        if (!cwd || cwd[0] == '\0')
            return false;
        JoinPath(candidate, sizeof(candidate), cwd, input);
    }
    NormalizePathSeparators(candidate);

#ifdef _WIN32
    char resolved[1024] = {0};
    if (_fullpath(resolved, candidate, sizeof(resolved)))
    {
        CopyStringSafe(out, outSize, resolved);
    }
    else
    {
        CopyStringSafe(out, outSize, candidate);
    }
#else
    char resolved[PATH_MAX] = {0};
    if (realpath(candidate, resolved))
        CopyStringSafe(out, outSize, resolved);
    else
        CopyStringSafe(out, outSize, candidate);
#endif
    NormalizePathSeparators(out);
    TrimTrailingSeparator(out);
    return true;
}

static bool PathsEqualLocal(const char *a, const char *b)
{
    char na[1024] = {0};
    char nb[1024] = {0};
    if (!NormalizeAbsolutePath(a, na, sizeof(na)) || !NormalizeAbsolutePath(b, nb, sizeof(nb)))
        return false;
#ifdef _WIN32
    return _stricmp(na, nb) == 0;
#else
    return strcmp(na, nb) == 0;
#endif
}

static bool EnsureDirectoryRecursive(const char *path)
{
    if (!path || path[0] == '\0')
        return false;
    if (DirectoryExists(path))
        return true;

    char temp[1024] = {0};
    CopyStringSafe(temp, sizeof(temp), path);
    NormalizePathSeparators(temp);
    TrimTrailingSeparator(temp);

    size_t start = 0;
#ifdef _WIN32
    if (temp[1] == ':')
        start = 3;
#endif

    for (size_t i = start; temp[i] != '\0'; i++)
    {
        if (temp[i] != '/' && temp[i] != '\\')
            continue;

        char saved = temp[i];
        temp[i] = '\0';
        if (temp[0] != '\0' && !DirectoryExists(temp))
        {
#ifdef _WIN32
            if (_mkdir(temp) != 0 && !DirectoryExists(temp))
                return false;
#else
            if (mkdir(temp, 0755) != 0 && !DirectoryExists(temp))
                return false;
#endif
        }
        temp[i] = saved;
    }

    if (!DirectoryExists(temp))
    {
#ifdef _WIN32
        if (_mkdir(temp) != 0 && !DirectoryExists(temp))
            return false;
#else
        if (mkdir(temp, 0755) != 0 && !DirectoryExists(temp))
            return false;
#endif
    }

    return true;
}

static bool CopyFileBinaryLocal(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in)
        return false;

    char dstDir[1024] = {0};
    GetDirectoryFromPathLocal(dst, dstDir, sizeof(dstDir));
    if (dstDir[0] != '\0' && strcmp(dstDir, ".") != 0)
    {
        if (!EnsureDirectoryRecursive(dstDir))
        {
            fclose(in);
            return false;
        }
    }

    FILE *out = fopen(dst, "wb");
    if (!out)
    {
        fclose(in);
        return false;
    }

    char buffer[8192];
    size_t bytesRead = 0;
    bool ok = true;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), in)) > 0)
    {
        if (fwrite(buffer, 1, bytesRead, out) != bytesRead)
        {
            ok = false;
            break;
        }
    }

    fclose(in);
    fclose(out);
    return ok;
}

static bool CopyDirectoryRecursive(const char *srcDir, const char *dstDir)
{
    if (!srcDir || srcDir[0] == '\0' || !DirectoryExists(srcDir))
        return false;
    if (!dstDir || dstDir[0] == '\0')
        return false;
    if (!EnsureDirectoryRecursive(dstDir))
        return false;

    FilePathList items = LoadDirectoryFiles(srcDir);
    bool ok = true;

    for (int i = 0; i < (int)items.count; i++)
    {
        const char *srcPath = items.paths[i];
        if (!srcPath || srcPath[0] == '\0')
            continue;

        const char *name = GetFileName(srcPath);
        if (!name || name[0] == '\0')
            continue;

        char dstPath[1024] = {0};
        JoinPath(dstPath, sizeof(dstPath), dstDir, name);

        if (!IsPathFile(srcPath) && DirectoryExists(srcPath))
        {
            if (!CopyDirectoryRecursive(srcPath, dstPath))
            {
                ok = false;
                break;
            }
        }
        else if (!CopyFileBinaryLocal(srcPath, dstPath))
        {
            ok = false;
            break;
        }
    }

    UnloadDirectoryFiles(items);
    return ok;
}

static bool TryResolvePathFromBaseChain(const char *baseDir, const char *relativePath, char *out, size_t outSize)
{
    if (!baseDir || baseDir[0] == '\0' || !relativePath || relativePath[0] == '\0' || !out || outSize == 0)
        return false;

    char current[1024] = {0};
    CopyStringSafe(current, sizeof(current), baseDir);
    NormalizePathSeparators(current);
    TrimTrailingSeparator(current);

    for (int i = 0; i < 6; i++)
    {
        JoinPath(out, outSize, current, relativePath);
        if (FileExists(out) || DirectoryExists(out))
            return true;

        char next[1024] = {0};
        JoinPath(next, sizeof(next), current, "..");
        if (strcmp(next, current) == 0)
            break;
        CopyStringSafe(current, sizeof(current), next);
    }

    out[0] = '\0';
    return false;
}

static bool ResolveRuntimePlayerPath(char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';

    const char *candidates[] = {
#ifdef _WIN32
        "NanquimoriPlayer.exe",
        ".cmake/build-debug/NanquimoriPlayer.exe",
        ".cmake/build-release/NanquimoriPlayer.exe",
        "build/NanquimoriPlayer.exe"
#else
        "NanquimoriPlayer",
        ".cmake/build-debug/NanquimoriPlayer",
        ".cmake/build-release/NanquimoriPlayer",
        "build/NanquimoriPlayer"
#endif
    };
    const char *appDir = GetApplicationDirectory();
    const char *cwd = GetWorkingDirectory();
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++)
    {
        const char *candidate = candidates[i];
        if (FileExists(candidate))
        {
            CopyStringSafe(out, outSize, candidate);
            NormalizePathSeparators(out);
            return true;
        }
        if (TryResolvePathFromBaseChain(appDir, candidate, out, outSize))
            return true;
        if (TryResolvePathFromBaseChain(cwd, candidate, out, outSize))
            return true;
    }

    return false;
}

static bool ResolveShadersDirectory(char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';

    if (DirectoryExists("shaders"))
    {
        CopyStringSafe(out, outSize, "shaders");
        NormalizePathSeparators(out);
        return true;
    }

    const char *appDir = GetApplicationDirectory();
    if (TryResolvePathFromBaseChain(appDir, "shaders", out, outSize))
        return true;

    const char *cwd = GetWorkingDirectory();
    if (TryResolvePathFromBaseChain(cwd, "shaders", out, outSize))
        return true;

    return false;
}

static bool ResolvePathAgainstProject(const char *projectDir, const char *path, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';
    if (!path || path[0] == '\0')
        return false;

    if (IsPathAbsoluteLocal(path))
    {
        CopyStringSafe(out, outSize, path);
        NormalizePathSeparators(out);
        return true;
    }

    if (projectDir && projectDir[0] != '\0')
    {
        JoinPath(out, outSize, projectDir, path);
        return true;
    }

    CopyStringSafe(out, outSize, path);
    NormalizePathSeparators(out);
    return true;
}

static bool ResolveIconSourcePath(const char *projectDir, const ProjectExportSettings *settings, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';

    if (settings && settings->iconPath[0] != '\0')
    {
        if (ResolvePathAgainstProject(projectDir, settings->iconPath, out, outSize) && IsPathFile(out))
            return true;
    }

    if (projectDir && projectDir[0] != '\0')
    {
        JoinPath(out, outSize, projectDir, "icon.png");
        if (FileExists(out))
            return true;
    }

    out[0] = '\0';
    return false;
}

static bool SaveSquarePngForIcon(const char *sourceImagePath, const char *pngOutPath)
{
    if (!sourceImagePath || sourceImagePath[0] == '\0' || !pngOutPath || pngOutPath[0] == '\0')
        return false;

    Image source = LoadImage(sourceImagePath);
    if (!source.data || source.width <= 0 || source.height <= 0)
        return false;

    const int targetSize = 256;
    float scale = 1.0f;
    if (source.width > source.height)
        scale = (float)targetSize / (float)source.width;
    else
        scale = (float)targetSize / (float)source.height;

    int resizedW = (int)((float)source.width * scale + 0.5f);
    int resizedH = (int)((float)source.height * scale + 0.5f);
    if (resizedW < 1)
        resizedW = 1;
    if (resizedH < 1)
        resizedH = 1;

    ImageResize(&source, resizedW, resizedH);

    Image canvas = GenImageColor(targetSize, targetSize, (Color){0, 0, 0, 0});
    Rectangle srcRec = {0.0f, 0.0f, (float)source.width, (float)source.height};
    Rectangle dstRec = {
        (float)((targetSize - source.width) / 2),
        (float)((targetSize - source.height) / 2),
        (float)source.width,
        (float)source.height};
    ImageDraw(&canvas, source, srcRec, dstRec, WHITE);

    bool ok = ExportImage(canvas, pngOutPath);
    UnloadImage(canvas);
    UnloadImage(source);
    return ok;
}

#pragma pack(push, 1)
typedef struct
{
    uint16_t reserved;
    uint16_t type;
    uint16_t count;
} IconDirHeader;

typedef struct
{
    uint8_t width;
    uint8_t height;
    uint8_t colorCount;
    uint8_t reserved;
    uint16_t planes;
    uint16_t bitCount;
    uint32_t bytesInRes;
    uint32_t imageOffset;
} IconDirEntry;
#pragma pack(pop)

static bool WriteIcoFromPng(const char *pngPath, const char *icoPath)
{
    int pngSize = 0;
    unsigned char *pngData = LoadFileData(pngPath, &pngSize);
    if (!pngData || pngSize <= 0)
        return false;

    FILE *out = fopen(icoPath, "wb");
    if (!out)
    {
        UnloadFileData(pngData);
        return false;
    }

    IconDirHeader header = {0, 1, 1};
    IconDirEntry entry = {0};
    entry.width = 0;
    entry.height = 0;
    entry.colorCount = 0;
    entry.reserved = 0;
    entry.planes = 1;
    entry.bitCount = 32;
    entry.bytesInRes = (uint32_t)pngSize;
    entry.imageOffset = sizeof(IconDirHeader) + sizeof(IconDirEntry);

    bool ok = fwrite(&header, 1, sizeof(header), out) == sizeof(header) &&
              fwrite(&entry, 1, sizeof(entry), out) == sizeof(entry) &&
              fwrite(pngData, 1, (size_t)pngSize, out) == (size_t)pngSize;

    fclose(out);
    UnloadFileData(pngData);
    return ok;
}

static bool CreateExportIco(const char *sourceIconPath, const char *icoOutPath)
{
    if (!sourceIconPath || sourceIconPath[0] == '\0' || !icoOutPath || icoOutPath[0] == '\0')
        return false;

    const char *ext = GetFileExtension(sourceIconPath);
    if (ext && (strcmp(ext, ".ico") == 0 || strcmp(ext, ".ICO") == 0))
        return CopyFileBinaryLocal(sourceIconPath, icoOutPath);

    char iconDir[1024] = {0};
    char pngOutPath[1024] = {0};
    GetDirectoryFromPathLocal(icoOutPath, iconDir, sizeof(iconDir));
    JoinPath(pngOutPath, sizeof(pngOutPath), iconDir, "game_icon.png");

    if (!SaveSquarePngForIcon(sourceIconPath, pngOutPath))
        return false;

    return WriteIcoFromPng(pngOutPath, icoOutPath);
}

static void MergeExportSettings(ProjectExportSettings *target, const ProjectExportSettings *overrideSettings)
{
    if (!target || !overrideSettings)
        return;

    if (overrideSettings->gameName[0] != '\0')
        CopyStringSafe(target->gameName, sizeof(target->gameName), overrideSettings->gameName);
    if (overrideSettings->exeName[0] != '\0')
        CopyStringSafe(target->exeName, sizeof(target->exeName), overrideSettings->exeName);
    if (overrideSettings->iconPath[0] != '\0')
        CopyStringSafe(target->iconPath, sizeof(target->iconPath), overrideSettings->iconPath);
    if (overrideSettings->outputDir[0] != '\0')
        CopyStringSafe(target->outputDir, sizeof(target->outputDir), overrideSettings->outputDir);
    if (overrideSettings->windowWidth > 0)
        target->windowWidth = overrideSettings->windowWidth;
    if (overrideSettings->windowHeight > 0)
        target->windowHeight = overrideSettings->windowHeight;
    target->showConsole = overrideSettings->showConsole;
    target->fullscreenMode = overrideSettings->fullscreenMode;
    target->startFullscreen = overrideSettings->startFullscreen;
    target->startMaximized = overrideSettings->startMaximized;
    target->resizableWindow = overrideSettings->resizableWindow;
    target->showStartupHud = overrideSettings->showStartupHud;
}

bool ExportGameBuild(const ProjectExportSettings *settings, char *status, size_t statusSize)
{
    if (status && statusSize > 0)
        status[0] = '\0';

    const char *projectPath = GetProjectPath();
    const char *projectDir = GetProjectDir();
    if (!projectPath || projectPath[0] == '\0' || !projectDir || projectDir[0] == '\0')
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "Salve o projeto antes de exportar.");
        return false;
    }

    ProjectExportSettings effective = {0};
    GetProjectExportSettings(&effective);
    if (settings)
        MergeExportSettings(&effective, settings);

    if (effective.exeName[0] == '\0')
        CopyStringSafe(effective.exeName, sizeof(effective.exeName), "jogo");

    char outputDir[1024] = {0};
    if (!ResolvePathAgainstProject(projectDir, effective.outputDir, outputDir, sizeof(outputDir)))
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "Pasta de saida invalida.");
        return false;
    }
    NormalizePathSeparators(outputDir);
    TrimTrailingSeparator(outputDir);

    if (outputDir[0] == '\0')
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "Pasta de saida invalida.");
        return false;
    }

    if (PathsEqualLocal(projectDir, outputDir))
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "A pasta de exportacao nao pode ser a pasta do projeto.");
        return false;
    }

    if (!EnsureDirectoryRecursive(outputDir))
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "Nao foi possivel criar a pasta de exportacao.");
        return false;
    }

    char runtimeExeSource[1024] = {0};
    if (!ResolveRuntimePlayerPath(runtimeExeSource, sizeof(runtimeExeSource)))
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "Nao encontrei o executavel base NanquimoriPlayer.exe.");
        return false;
    }

    char shadersSourceDir[1024] = {0};
    if (!ResolveShadersDirectory(shadersSourceDir, sizeof(shadersSourceDir)))
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "Nao encontrei a pasta shaders para incluir na exportacao.");
        return false;
    }

    char safeExeName[128] = {0};
    CopyStringSafe(safeExeName, sizeof(safeExeName), effective.exeName);
    for (size_t i = 0; safeExeName[i] != '\0'; i++)
    {
        char c = safeExeName[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            safeExeName[i] = '_';
    }
    if (safeExeName[0] == '\0')
        CopyStringSafe(safeExeName, sizeof(safeExeName), "jogo");

    char exportedExePath[1024] = {0};
    char exeFileName[160] = {0};
    snprintf(exeFileName, sizeof(exeFileName), "%s.exe", safeExeName);
    JoinPath(exportedExePath, sizeof(exportedExePath), outputDir, exeFileName);

    char exportedProjectPath[1024] = {0};
    JoinPath(exportedProjectPath, sizeof(exportedProjectPath), outputDir, "project.json");
    if (!CopyFileBinaryLocal(projectPath, exportedProjectPath))
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "Falha ao copiar project.json para a exportacao.");
        return false;
    }

    char assetsOutputDir[1024] = {0};
    JoinPath(assetsOutputDir, sizeof(assetsOutputDir), outputDir, "assets");
    if (!EnsureDirectoryRecursive(assetsOutputDir))
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "Falha ao preparar a pasta assets da exportacao.");
        return false;
    }

    char assetsProjectDir[1024] = {0};
    JoinPath(assetsProjectDir, sizeof(assetsProjectDir), projectDir, "assets");
    if (DirectoryExists(assetsProjectDir) && !CopyDirectoryRecursive(assetsProjectDir, assetsOutputDir))
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "Falha ao copiar os assets do projeto.");
        return false;
    }

    char shadersOutputDir[1024] = {0};
    JoinPath(shadersOutputDir, sizeof(shadersOutputDir), outputDir, "shaders");
    if (!CopyDirectoryRecursive(shadersSourceDir, shadersOutputDir))
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "Falha ao copiar os shaders necessarios.");
        return false;
    }

    if (!CopyFileBinaryLocal(runtimeExeSource, exportedExePath))
    {
        if (status && statusSize > 0)
            snprintf(status, statusSize, "Falha ao copiar o executavel do player.");
        return false;
    }

    char iconSourcePath[1024] = {0};
    bool hasCustomIcon = ResolveIconSourcePath(projectDir, &effective, iconSourcePath, sizeof(iconSourcePath));
    bool iconApplied = false;
    if (hasCustomIcon)
    {
        char exportedIcoPath[1024] = {0};
        JoinPath(exportedIcoPath, sizeof(exportedIcoPath), outputDir, "game.ico");
        if (CreateExportIco(iconSourcePath, exportedIcoPath))
            iconApplied = ReplaceExecutableIconFromIco(exportedExePath, exportedIcoPath);
    }

    if (status && statusSize > 0)
    {
        if (hasCustomIcon && !iconApplied)
            snprintf(status, statusSize, "Jogo exportado para %s, mas o icone do .exe nao foi aplicado.", outputDir);
        else
            snprintf(status, statusSize, "Jogo exportado com sucesso em %s", outputDir);
    }

    return true;
}
