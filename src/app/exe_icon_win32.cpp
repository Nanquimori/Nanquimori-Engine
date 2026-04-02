#include "exe_icon_win32.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOMINMAX
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

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

typedef struct
{
    uint16_t reserved;
    uint16_t type;
    uint16_t count;
} GrpIconDirHeader;

typedef struct
{
    uint8_t width;
    uint8_t height;
    uint8_t colorCount;
    uint8_t reserved;
    uint16_t planes;
    uint16_t bitCount;
    uint32_t bytesInRes;
    uint16_t resourceId;
} GrpIconDirEntry;
#pragma pack(pop)

static bool ReadWholeFile(const char *path, std::vector<unsigned char> &outData)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0)
    {
        fclose(file);
        return false;
    }

    outData.resize((size_t)size);
    bool ok = fread(outData.data(), 1, outData.size(), file) == outData.size();
    fclose(file);
    return ok;
}

bool ReplaceExecutableIconFromIco(const char *exePath, const char *icoPath)
{
    if (!exePath || exePath[0] == '\0' || !icoPath || icoPath[0] == '\0')
        return false;

    std::vector<unsigned char> iconFileData;
    if (!ReadWholeFile(icoPath, iconFileData) || iconFileData.size() < sizeof(IconDirHeader))
        return false;

    IconDirHeader *iconHeader = (IconDirHeader *)iconFileData.data();
    if (iconHeader->reserved != 0 || iconHeader->type != 1 || iconHeader->count == 0)
        return false;

    size_t entriesSize = (size_t)iconHeader->count * sizeof(IconDirEntry);
    if (iconFileData.size() < sizeof(IconDirHeader) + entriesSize)
        return false;

    IconDirEntry *iconEntries = (IconDirEntry *)(iconFileData.data() + sizeof(IconDirHeader));
    GrpIconDirHeader groupHeader = {0, 1, iconHeader->count};
    size_t groupSize = sizeof(groupHeader) + (size_t)iconHeader->count * sizeof(GrpIconDirEntry);
    std::vector<unsigned char> groupData(groupSize, 0);
    memcpy(groupData.data(), &groupHeader, sizeof(groupHeader));

    GrpIconDirEntry *groupEntries = (GrpIconDirEntry *)(groupData.data() + sizeof(groupHeader));
    for (uint16_t i = 0; i < iconHeader->count; i++)
    {
        IconDirEntry *src = &iconEntries[i];
        if ((uint64_t)src->imageOffset + (uint64_t)src->bytesInRes > (uint64_t)iconFileData.size())
            return false;

        groupEntries[i].width = src->width;
        groupEntries[i].height = src->height;
        groupEntries[i].colorCount = src->colorCount;
        groupEntries[i].reserved = src->reserved;
        groupEntries[i].planes = src->planes;
        groupEntries[i].bitCount = src->bitCount;
        groupEntries[i].bytesInRes = src->bytesInRes;
        groupEntries[i].resourceId = (uint16_t)(101 + i);
    }

    wchar_t exeWide[1024] = {0};
    if (!MultiByteToWideChar(CP_UTF8, 0, exePath, -1, exeWide, (int)(sizeof(exeWide) / sizeof(exeWide[0]))))
        MultiByteToWideChar(CP_ACP, 0, exePath, -1, exeWide, (int)(sizeof(exeWide) / sizeof(exeWide[0])));
    if (exeWide[0] == L'\0')
        return false;

    HANDLE updateHandle = BeginUpdateResourceW(exeWide, FALSE);
    if (!updateHandle)
        return false;

    bool ok = true;
    WORD langId = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    LPCWSTR iconType = MAKEINTRESOURCEW(3);
    LPCWSTR groupIconType = MAKEINTRESOURCEW(14);

    for (uint16_t i = 0; i < iconHeader->count; i++)
    {
        IconDirEntry *src = &iconEntries[i];
        if (!UpdateResourceW(updateHandle,
                             iconType,
                             MAKEINTRESOURCEW(101 + i),
                             langId,
                             iconFileData.data() + src->imageOffset,
                             src->bytesInRes))
        {
            ok = false;
            break;
        }
    }

    if (ok)
    {
        ok = UpdateResourceW(updateHandle,
                             groupIconType,
                             MAKEINTRESOURCEW(1),
                             langId,
                             groupData.data(),
                             (DWORD)groupData.size()) != FALSE;
    }

    if (!EndUpdateResourceW(updateHandle, ok ? FALSE : TRUE))
        ok = false;

    return ok;
}

#else

bool ReplaceExecutableIconFromIco(const char *exePath, const char *icoPath)
{
    (void)exePath;
    (void)icoPath;
    return true;
}

#endif
