#include "top_bar.h"
#include "export_dialog.h"
#include "file_explorer.h"
#include "help_panel.h"
#include "splash_screen.h"
#include "app/application.h"
#include "editor/viewport/camera_controller.h"
#include "scene/outliner.h"
#include "scene/scene_camera.h"
#include "properties_panel.h"
#include "assets/model_manager.h"
#include "ui_button.h"
#include "ui_style.h"
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000003
#endif
#define WIN32_LEAN_AND_MEAN
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define LoadImage Win32LoadImage
#define DrawText Win32DrawText
#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_1.h>
#include <d2d1_3.h>
#include <d2d1_3helper.h>
#undef Rectangle
#undef CloseWindow
#undef ShowCursor
#undef LoadImage
#undef DrawText
#endif

// -------------------------------------------------------
// STATIC TOP BAR STATE
// -------------------------------------------------------
static Texture2D iconFolder = {0};
static Texture2D iconHelp = {0};
static Texture2D iconN = {0};
static Texture2D iconWireframe = {0};
static bool playModeActive = false;
static bool playPaused = false;
static bool playStopRequested = false;
static bool playRestartRequested = false;
static bool fileHover = false;
static bool helpHover = false;
static bool buildHover = false;
static bool playHover = false;
static bool stopHover = false;
static bool restartHover = false;
static bool navigateHover = false;
static bool viewportWireframeHover = false;
static bool viewportNavigateMode = false;
static bool viewportWireframeMode = false;
static bool addMenuOpen = false;
static bool addHover = false;
static int addMainHoverItem = -1;
static int addShapeHoverItem = -1;
static bool addShapesSubmenuOpen = false;
static const PrimitiveModelType addPrimitiveTypes[] = {
    PRIMITIVE_MODEL_CUBE,
    PRIMITIVE_MODEL_SPHERE,
    PRIMITIVE_MODEL_CYLINDER,
    PRIMITIVE_MODEL_PLANE};
static const char *addPrimitiveLabels[] = {
    "Cube",
    "Sphere",
    "Cylinder",
    "Plane"};
static const char *topBarBrandText = "Nanquimori Engine";

static Color TopBarMenuHoverColor(void)
{
    return (Color){58, 26, 24, 255};
}

static Color TopBarMenuBorderColor(void)
{
    return (Color){104, 56, 52, 255};
}

static const char *GetViewportModeName(bool wireframe)
{
    return wireframe ? "Wireframe" : "Solid";
}

static Rectangle GetTopBarWireframeButtonRect(void)
{
    const float rightPadding = 8.0f;
    const float buttonWidth = 30.0f;
    const float buttonHeight = 24.0f;
    return (Rectangle){
        (float)GetScreenWidth() - (float)PROPERTIES_PAINEL_LARGURA - rightPadding - buttonWidth,
        0.0f,
        buttonWidth,
        buttonHeight};
}

static float GetTopBarRightViewportModeX(void)
{
    return GetTopBarWireframeButtonRect().x;
}

static bool TryResolvePathFromBaseChain(const char *baseDir, const char *relativePath, char *out, size_t outSize)
{
    if (!baseDir || baseDir[0] == '\0' || !relativePath || relativePath[0] == '\0' || !out || outSize == 0)
        return false;

    char current[512] = {0};
    strncpy(current, baseDir, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';

    for (int i = 0; i < 6; i++)
    {
        snprintf(out, outSize, "%s/%s", current, relativePath);
        out[outSize - 1] = '\0';
        if (FileExists(out))
            return true;

        char next[512] = {0};
        snprintf(next, sizeof(next), "%s/..", current);
        next[sizeof(next) - 1] = '\0';
        if (strcmp(next, current) == 0)
            break;
        strncpy(current, next, sizeof(current) - 1);
        current[sizeof(current) - 1] = '\0';
    }

    return false;
}

static bool ResolveAssetPath(const char *relativePath, char *out, size_t outSize)
{
    if (!relativePath || relativePath[0] == '\0' || !out || outSize == 0)
        return false;
    out[0] = '\0';

    if (FileExists(relativePath))
    {
        strncpy(out, relativePath, outSize - 1);
        out[outSize - 1] = '\0';
        return true;
    }

    const char *cwd = GetWorkingDirectory();
    const char *appDir = GetApplicationDirectory();
    if (TryResolvePathFromBaseChain(cwd, relativePath, out, outSize))
        return true;
    if (TryResolvePathFromBaseChain(appDir, relativePath, out, outSize))
        return true;

    return false;
}

#ifdef _WIN32
template <typename T>
static void ReleaseCom(T **value)
{
    if (!value || !*value)
        return;
    (*value)->Release();
    *value = nullptr;
}

static bool ParseSvgViewportSize(const unsigned char *data, int dataSize, float *outWidth, float *outHeight)
{
    if (!data || dataSize <= 0 || !outWidth || !outHeight)
        return false;

    std::string text((const char *)data, (size_t)dataSize);

    size_t viewBoxPos = text.find("viewBox=");
    if (viewBoxPos != std::string::npos && viewBoxPos + 9 < text.size())
    {
        char quote = text[viewBoxPos + 8];
        size_t start = viewBoxPos + 9;
        size_t end = text.find(quote, start);
        if (end != std::string::npos)
        {
            float minX = 0.0f;
            float minY = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            std::string viewBox = text.substr(start, end - start);
            if (sscanf(viewBox.c_str(), "%f %f %f %f", &minX, &minY, &width, &height) == 4 &&
                width > 0.0f && height > 0.0f)
            {
                *outWidth = width;
                *outHeight = height;
                return true;
            }
        }
    }

    size_t widthPos = text.find("width=");
    size_t heightPos = text.find("height=");
    if (widthPos != std::string::npos && heightPos != std::string::npos &&
        widthPos + 8 < text.size() && heightPos + 9 < text.size())
    {
        char widthQuote = text[widthPos + 6];
        char heightQuote = text[heightPos + 7];
        size_t widthStart = widthPos + 7;
        size_t heightStart = heightPos + 8;
        size_t widthEnd = text.find(widthQuote, widthStart);
        size_t heightEnd = text.find(heightQuote, heightStart);
        if (widthEnd != std::string::npos && heightEnd != std::string::npos)
        {
            float width = 0.0f;
            float height = 0.0f;
            std::string widthText = text.substr(widthStart, widthEnd - widthStart);
            std::string heightText = text.substr(heightStart, heightEnd - heightStart);
            if (sscanf(widthText.c_str(), "%f", &width) == 1 &&
                sscanf(heightText.c_str(), "%f", &height) == 1 &&
                width > 0.0f && height > 0.0f)
            {
                *outWidth = width;
                *outHeight = height;
                return true;
            }
        }
    }

    return false;
}

static Texture2D LoadWin32SvgTextureAsset(const char *relativePath, int rasterSize)
{
    char path[512] = {0};
    if (!ResolveAssetPath(relativePath, path, sizeof(path)))
        return (Texture2D){0};

    int dataSize = 0;
    unsigned char *fileData = LoadFileData(path, &dataSize);
    if (!fileData || dataSize <= 0)
        return (Texture2D){0};

    float svgWidth = (float)rasterSize;
    float svgHeight = (float)rasterSize;
    ParseSvgViewportSize(fileData, dataSize, &svgWidth, &svgHeight);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool shouldUninitialize = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        UnloadFileData(fileData);
        return (Texture2D){0};
    }

    Texture2D result = {0};
    IStream *stream = SHCreateMemStream((const BYTE *)fileData, (UINT)dataSize);
    ID3D11Device *d3dDevice = nullptr;
    ID3D11DeviceContext *d3dContext = nullptr;
    IDXGIDevice *dxgiDevice = nullptr;
    ID2D1Factory6 *d2dFactory = nullptr;
    ID2D1Device5 *d2dDevice = nullptr;
    ID2D1DeviceContext5 *d2dContext5 = nullptr;
    ID2D1SvgDocument *svgDocument = nullptr;
    ID3D11Texture2D *renderTexture = nullptr;
    ID3D11Texture2D *stagingTexture = nullptr;
    IDXGISurface *dxgiSurface = nullptr;
    ID2D1Bitmap1 *targetBitmap = nullptr;
    UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    D2D1_FACTORY_OPTIONS factoryOptions = {};
    D3D11_TEXTURE2D_DESC textureDesc = {};
    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Image image = {0};

    if (!stream)
        goto cleanup;

    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags,
                           nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);
    if (FAILED(hr))
    {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, deviceFlags,
                               nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);
    }
    if (FAILED(hr) || !d3dDevice || !d3dContext)
        goto cleanup;

    hr = d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr) || !dxgiDevice)
        goto cleanup;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           __uuidof(ID2D1Factory6),
                           &factoryOptions,
                           (void **)&d2dFactory);
    if (FAILED(hr) || !d2dFactory)
        goto cleanup;

    hr = d2dFactory->CreateDevice(dxgiDevice, &d2dDevice);
    if (FAILED(hr) || !d2dDevice)
        goto cleanup;

    hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext5);
    if (FAILED(hr) || !d2dContext5)
        goto cleanup;

    hr = d2dContext5->CreateSvgDocument(stream, D2D1::SizeF(svgWidth, svgHeight), &svgDocument);
    if (FAILED(hr) || !svgDocument)
        goto cleanup;

    textureDesc.Width = (UINT)rasterSize;
    textureDesc.Height = (UINT)rasterSize;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    hr = d3dDevice->CreateTexture2D(&textureDesc, nullptr, &renderTexture);
    if (FAILED(hr) || !renderTexture)
        goto cleanup;

    hr = renderTexture->QueryInterface(IID_PPV_ARGS(&dxgiSurface));
    if (FAILED(hr) || !dxgiSurface)
        goto cleanup;

    hr = d2dContext5->CreateBitmapFromDxgiSurface(dxgiSurface, &bitmapProperties, &targetBitmap);
    if (FAILED(hr) || !targetBitmap)
        goto cleanup;

    d2dContext5->SetTarget(targetBitmap);
    d2dContext5->BeginDraw();
    d2dContext5->Clear(D2D1::ColorF(0, 0.0f));
    d2dContext5->SetTransform(D2D1::Matrix3x2F::Scale((float)rasterSize / svgWidth, (float)rasterSize / svgHeight));
    d2dContext5->DrawSvgDocument(svgDocument);
    hr = d2dContext5->EndDraw();
    d2dContext5->SetTarget(nullptr);
    if (FAILED(hr))
        goto cleanup;

    textureDesc.Usage = D3D11_USAGE_STAGING;
    textureDesc.BindFlags = 0;
    textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = d3dDevice->CreateTexture2D(&textureDesc, nullptr, &stagingTexture);
    if (FAILED(hr) || !stagingTexture)
        goto cleanup;

    d3dContext->CopyResource(stagingTexture, renderTexture);

    hr = d3dContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
        goto cleanup;

    image.width = rasterSize;
    image.height = rasterSize;
    image.mipmaps = 1;
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    image.data = MemAlloc((size_t)rasterSize * (size_t)rasterSize * 4u);
    if (!image.data)
    {
        d3dContext->Unmap(stagingTexture, 0);
        goto cleanup;
    }

    for (int y = 0; y < rasterSize; y++)
    {
        unsigned char *dst = (unsigned char *)image.data + (size_t)y * (size_t)rasterSize * 4u;
        const unsigned char *src = (const unsigned char *)mapped.pData + (size_t)y * mapped.RowPitch;
        for (int x = 0; x < rasterSize; x++)
        {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }

    d3dContext->Unmap(stagingTexture, 0);
    result = LoadTextureFromImage(image);
    UnloadImage(image);

cleanup:
    ReleaseCom(&targetBitmap);
    ReleaseCom(&dxgiSurface);
    ReleaseCom(&stagingTexture);
    ReleaseCom(&renderTexture);
    ReleaseCom(&svgDocument);
    ReleaseCom(&d2dContext5);
    ReleaseCom(&d2dDevice);
    ReleaseCom(&d2dFactory);
    ReleaseCom(&dxgiDevice);
    ReleaseCom(&d3dContext);
    ReleaseCom(&d3dDevice);
    if (stream)
        stream->Release();
    UnloadFileData(fileData);
    if (shouldUninitialize)
        CoUninitialize();
    return result;
}
#endif

static Texture2D LoadTopBarTextureAsset(const char *relativePath, bool trimAlpha = false)
{
    char path[512] = {0};
    if (!ResolveAssetPath(relativePath, path, sizeof(path)))
        return (Texture2D){0};

    Image image = LoadImage(path);
    if (!image.data || image.width <= 0 || image.height <= 0)
        return (Texture2D){0};

    Rectangle alphaBorder = GetImageAlphaBorder(image, 0.01f);
    if (trimAlpha &&
        alphaBorder.width > 0.0f && alphaBorder.height > 0.0f &&
        (alphaBorder.width < image.width || alphaBorder.height < image.height))
    {
        int borderX = (int)alphaBorder.x - 2;
        int borderY = (int)alphaBorder.y - 2;
        int borderW = (int)alphaBorder.width + 4;
        int borderH = (int)alphaBorder.height + 4;
        if (borderX < 0)
            borderX = 0;
        if (borderY < 0)
            borderY = 0;
        if (borderX + borderW > image.width)
            borderW = image.width - borderX;
        if (borderY + borderH > image.height)
            borderH = image.height - borderY;
        if (borderW > 0 && borderH > 0)
            ImageCrop(&image, (Rectangle){(float)borderX, (float)borderY, (float)borderW, (float)borderH});
    }

    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

static void DrawWireframeToggleIcon(Rectangle area, bool hovered, bool active)
{
    const UIStyle *style = GetUIStyle();
    Color iconColor = active ? (Color){244, 170, 90, 255}
                             : (hovered ? style->textPrimary : style->textSecondary);
    float iconSize = 20.0f;
    Vector2 iconPos = {
        area.x + area.width * 0.5f - iconSize * 0.5f,
        area.y + area.height * 0.5f - iconSize * 0.5f};

    if (hovered || active)
    {
        Rectangle visualArea = {
            area.x + 2.0f,
            area.y + 2.0f,
            area.width - 4.0f,
            area.height - 4.0f};
        Color glow = active ? Fade(iconColor, 0.16f) : Fade(style->textPrimary, 0.07f);
        DrawRectangleRounded(visualArea, 0.45f, 8, glow);
    }

    if (iconWireframe.id <= 0 || iconWireframe.width <= 0 || iconWireframe.height <= 0)
        return;

    DrawTextureEx(iconWireframe, iconPos, 0.0f, iconSize / (float)iconWireframe.width, iconColor);
}

static void DrawTopBarIcon(Texture2D icon, Vector2 pos, float size, Color tint)
{
    if (icon.id <= 0 || icon.width <= 0 || icon.height <= 0)
        return;
    DrawTextureEx(icon, pos, 0.0f, size / (float)icon.width, tint);
}

static void UnloadTopBarIcon(Texture2D *icon)
{
    if (!icon || icon->id <= 0)
        return;
    UnloadTexture(*icon);
    *icon = (Texture2D){0};
}

static void BuildUniqueObjectName(const char *baseName, char *outName, size_t outSize)
{
    if (!outName || outSize == 0)
        return;
    outName[0] = '\0';

    const char *base = (baseName && baseName[0] != '\0') ? baseName : "Object";
    if (!ObjetoExisteNoOutliner(base))
    {
        strncpy(outName, base, outSize - 1);
        outName[outSize - 1] = '\0';
        return;
    }

    for (int i = 1; i < 1000; i++)
    {
        snprintf(outName, outSize, "%s %d", base, i);
        outName[outSize - 1] = '\0';
        if (!ObjetoExisteNoOutliner(outName))
            return;
    }

    strncpy(outName, base, outSize - 1);
    outName[outSize - 1] = '\0';
}

static void AddEmptyObject(void)
{
    char objectName[32] = {0};
    BuildUniqueObjectName("Empty", objectName, sizeof(objectName));
    int id = RegistrarObjeto(objectName, (Vector3){0, 0, 0}, -1);
    if (id <= 0)
        return;

    int idx = BuscarIndicePorId(id);
    if (idx != -1)
        objetos[idx].caminhoModelo[0] = '\0';

    SelecionarObjetoPorId(id);
    SetSelectedModelByObjetoId(id);
}

// -------------------------------------------------------
// INIT / UNLOAD
// -------------------------------------------------------
void InitTopBar()
{
    iconFolder = LoadTopBarTextureAsset("icons/window.png");
    iconHelp = LoadTopBarTextureAsset("icons/help.png");
#ifdef _WIN32
    iconWireframe = LoadWin32SvgTextureAsset("icons/wireframe.svg", 256);
#else
    iconWireframe = LoadTopBarTextureAsset("icons/wireframe.svg", false);
#endif

    iconN = LoadTopBarTextureAsset("icons/N.ico");
    if (iconN.id <= 0)
        iconN = LoadTopBarTextureAsset("icons/n.png");
}

void UnloadTopBar()
{
    UnloadTopBarIcon(&iconFolder);
    UnloadTopBarIcon(&iconHelp);
    UnloadTopBarIcon(&iconN);
    UnloadTopBarIcon(&iconWireframe);
}

// -------------------------------------------------------
// UPDATE
// -------------------------------------------------------
void UpdateTopBar()
{
    float W = (float)GetScreenWidth();
    float left = (float)PAINEL_LARGURA;
    float right = (float)PROPERTIES_PAINEL_LARGURA;
    float availableW = W - left - right;
    float barY = 4.0f;
    float iconSize = 16.0f;
    float brandW = iconSize + 6.0f + (float)MeasureText(topBarBrandText, 12);
    float brandX = left + (availableW - brandW) * 0.5f;
    Rectangle areaBrand = {brandX, barY, brandW, 16.0f};
    UIButtonState brandState = UIButtonGetState(areaBrand);
    if (brandState.clicked)
        ShowSplashScreen();

    float barX = 8.0f + left;

    // File
    Rectangle areaFile = {barX - 4.0f, 2.0f, 64.0f, 20.0f};
    UIButtonState fileState = UIButtonGetState(areaFile);
    fileHover = fileState.hovered;
    if (fileState.clicked)
    {
        ToggleFileMenu();
        addMenuOpen = false;
        addShapesSubmenuOpen = false;
    }

    // Add
    barX += 70.0f;
    Rectangle areaAdd = {barX - 6.0f, 2.0f, 56.0f, 20.0f};
    UIButtonState addState = UIButtonGetState(areaAdd);
    addHover = addState.hovered;
    bool addToggledThisFrame = false;
    if (addState.clicked)
    {
        addMenuOpen = !addMenuOpen;
        addToggledThisFrame = true;
    }

    const float addItemH = 24.0f;
    Rectangle addMenuRect = {areaAdd.x, 24.0f, 220.0f, addItemH * 3.0f};
    Rectangle itemEmpty = {addMenuRect.x, addMenuRect.y, addMenuRect.width, addItemH};
    Rectangle itemCamera = {addMenuRect.x, addMenuRect.y + addItemH, addMenuRect.width, addItemH};
    Rectangle itemGeometry = {addMenuRect.x, addMenuRect.y + addItemH * 2.0f, addMenuRect.width, addItemH};
    const int addShapeCount = (int)(sizeof(addPrimitiveTypes) / sizeof(addPrimitiveTypes[0]));
    Rectangle addShapesRect = {addMenuRect.x + addMenuRect.width + 2.0f, itemGeometry.y, 180.0f, addShapeCount * addItemH};

    addMainHoverItem = -1;
    addShapeHoverItem = -1;
    if (addMenuOpen)
    {
        Vector2 mouse = GetMousePosition();
        bool clicked = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
        if (CheckCollisionPointRec(mouse, itemEmpty))
            addMainHoverItem = 0;
        else if (CheckCollisionPointRec(mouse, itemCamera))
            addMainHoverItem = 1;
        else if (CheckCollisionPointRec(mouse, itemGeometry))
            addMainHoverItem = 2;

        bool mouseInSubmenu = CheckCollisionPointRec(mouse, addShapesRect);
        addShapesSubmenuOpen = (addMainHoverItem == 2) || mouseInSubmenu;

        if (addShapesSubmenuOpen)
        {
            for (int i = 0; i < addShapeCount; i++)
            {
                Rectangle item = {addShapesRect.x, addShapesRect.y + i * addItemH, addShapesRect.width, addItemH};
                if (CheckCollisionPointRec(mouse, item))
                {
                    addShapeHoverItem = i;
                    break;
                }
            }
        }

        if (clicked && !addToggledThisFrame)
        {
            if (addMainHoverItem == 0)
            {
                AddEmptyObject();
                addMenuOpen = false;
                addShapesSubmenuOpen = false;
            }
            else if (addMainHoverItem == 1)
            {
                Camera viewCamera = GetEditorViewportCamera();
                AddCameraObjectFromView(&viewCamera);
                addMenuOpen = false;
                addShapesSubmenuOpen = false;
            }
            else if (addShapesSubmenuOpen && addShapeHoverItem >= 0)
            {
                AddPrimitiveObject(addPrimitiveTypes[addShapeHoverItem]);
                addMenuOpen = false;
                addShapesSubmenuOpen = false;
            }
            else if (!CheckCollisionPointRec(mouse, addMenuRect) && !CheckCollisionPointRec(mouse, addShapesRect))
            {
                addMenuOpen = false;
                addShapesSubmenuOpen = false;
            }
        }
    }

    // Build
    barX += 70.0f;
    Rectangle areaBuild = {barX - 6.0f, 2.0f, 58.0f, 20.0f};
    UIButtonState buildState = UIButtonGetState(areaBuild);
    buildHover = buildState.hovered;
    if (buildState.clicked)
    {
        OpenExportDialog();
        addMenuOpen = false;
        addShapesSubmenuOpen = false;
    }

    // Help
    barX += 62.0f;
    Rectangle areaHelp = {barX - 4.0f, 2.0f, 68.0f, 20.0f};
    UIButtonState helpState = UIButtonGetState(areaHelp);
    helpHover = helpState.hovered;
    if (helpState.clicked)
    {
        SetHelpPanelShow(!HelpPanelShouldShow());
        addMenuOpen = false;
        addShapesSubmenuOpen = false;
    }

    if (!playModeActive && viewportNavigateMode)
        viewportNavigateMode = false;

    // Visualizacao / Navegacao / Play / Stop / Restart (depois do Help)
    float btnY = barY;
    Rectangle areaWireframe = GetTopBarWireframeButtonRect();
    UIButtonState wireframeState = UIButtonGetState(areaWireframe);
    viewportWireframeHover = wireframeState.hovered;
    if (wireframeState.clicked)
        viewportWireframeMode = !viewportWireframeMode;

    float btnX = barX + 80.0f;
    Rectangle areaPlay = {btnX, btnY, 60.0f, 16.0f};
    if (playModeActive)
        areaPlay.x = btnX + 94.0f;

    navigateHover = false;
    if (playModeActive)
    {
        Rectangle areaNavigate = {btnX, btnY, 84.0f, 16.0f};
        UIButtonState navigateState = UIButtonGetState(areaNavigate);
        navigateHover = navigateState.hovered;
        if (navigateState.clicked)
            viewportNavigateMode = !viewportNavigateMode;
    }

    UIButtonState playState = UIButtonGetState(areaPlay);
    playHover = playState.hovered;
    if (playState.clicked)
    {
        if (!playModeActive)
        {
            viewportNavigateMode = false;
            playModeActive = true;
            playPaused = false;
        }
        else if (playPaused)
        {
            playPaused = false;
        }
        else
        {
            playPaused = true;
        }
    }
    else if (playModeActive)
    {
        Rectangle areaStop = {btnX + 164.0f, btnY, 60.0f, 16.0f};
        Rectangle areaRestart = {btnX + 234.0f, btnY, 70.0f, 16.0f};
        UIButtonState stopState = UIButtonGetState(areaStop);
        UIButtonState restartState = UIButtonGetState(areaRestart);
        stopHover = stopState.hovered;
        restartHover = restartState.hovered;
        if (stopState.clicked)
        {
            playStopRequested = true;
        }
        else if (restartState.clicked)
        {
            playRestartRequested = true;
        }
    }
    else
    {
        stopHover = false;
        restartHover = false;
    }

}

// -------------------------------------------------------
// DRAW
// -------------------------------------------------------
void DrawTopBar()
{
    float W = (float)GetScreenWidth();
    float left = (float)PAINEL_LARGURA;
    float right = (float)PROPERTIES_PAINEL_LARGURA;
    float availableW = W - left - right;

    const UIStyle *style = GetUIStyle();
    DrawRectangle((int)left, 0, (int)availableW, 24, style->topBarBg);

    float barY = 4.0f;
    float iconSize = 16.0f;
    float brandW = iconSize + 6.0f + (float)MeasureText(topBarBrandText, 12);
    float brandX = left + (availableW - brandW) * 0.5f;
    DrawTopBarIcon(iconN, (Vector2){brandX, barY}, iconSize, style->textPrimary);
    DrawText(topBarBrandText, (int)(brandX + iconSize + 6.0f), 5, 12, style->textSecondary);

    float barX = 8.0f + left;

    // File
    Rectangle areaFile = {barX - 4.0f, 2.0f, 64.0f, 20.0f};
    bool fileActive = IsFileMenuOpen() || fileHover;
    Color fileText = fileActive ? style->accent : style->textPrimary;
    DrawTopBarIcon(iconFolder, (Vector2){barX, barY}, iconSize, style->textPrimary);
    DrawText("File", barX + 20.0f, 5, 12, fileText);

    // Add
    barX += 70.0f;
    float addMenuX = barX;
    Rectangle areaAdd = {barX - 6.0f, 2.0f, 56.0f, 20.0f};
    bool addActive = addMenuOpen || addHover;
    Color addColor = addActive ? style->accent : style->textPrimary;
    DrawText("Add", (int)barX, 5, 12, addColor);

    // Build
    barX += 70.0f;
    Rectangle areaBuild = {barX - 6.0f, 2.0f, 58.0f, 20.0f};
    bool buildActive = buildHover || IsExportDialogOpen();
    Color buildText = buildActive ? style->accent : style->textPrimary;
    DrawText("Build", (int)barX, 5, 12, buildText);

    // Help
    barX += 62.0f;
    Rectangle areaHelp = {barX - 4.0f, 2.0f, 68.0f, 20.0f};
    bool helpActive = helpHover || HelpPanelShouldShow();
    Color helpText = helpActive ? style->accent : style->textPrimary;
    DrawTopBarIcon(iconHelp, (Vector2){barX, barY}, iconSize, style->textPrimary);
    DrawText("Help", barX + 20.0f, 5, 12, helpText);

    if (addMenuOpen)
    {
        const float addItemH = 24.0f;
        Rectangle addMenuRect = {addMenuX, 24.0f, 220.0f, addItemH * 3.0f};
        Rectangle itemEmpty = {addMenuRect.x, addMenuRect.y, addMenuRect.width, addItemH};
        Rectangle itemCamera = {addMenuRect.x, addMenuRect.y + addItemH, addMenuRect.width, addItemH};
        Rectangle itemGeometry = {addMenuRect.x, addMenuRect.y + addItemH * 2.0f, addMenuRect.width, addItemH};
        const int addShapeCount = (int)(sizeof(addPrimitiveLabels) / sizeof(addPrimitiveLabels[0]));
        Rectangle addShapesRect = {addMenuRect.x + addMenuRect.width + 2.0f, itemGeometry.y, 180.0f, addShapeCount * addItemH};

        DrawRectangleRec(addMenuRect, style->buttonBg);
        DrawRectangleLinesEx(addMenuRect, 1.0f, style->panelBorder);
        DrawRectangleRec(itemEmpty, (addMainHoverItem == 0) ? TopBarMenuHoverColor() : style->buttonBg);
        DrawRectangleRec(itemCamera, (addMainHoverItem == 1) ? TopBarMenuHoverColor() : style->buttonBg);
        DrawRectangleRec(itemGeometry, (addMainHoverItem == 2 || addShapesSubmenuOpen) ? TopBarMenuHoverColor() : style->buttonBg);
        if (addMainHoverItem == 0)
            DrawRectangleLinesEx(itemEmpty, 1.0f, TopBarMenuBorderColor());
        if (addMainHoverItem == 1)
            DrawRectangleLinesEx(itemCamera, 1.0f, TopBarMenuBorderColor());
        if (addMainHoverItem == 2 || addShapesSubmenuOpen)
            DrawRectangleLinesEx(itemGeometry, 1.0f, TopBarMenuBorderColor());
        DrawText("Empty Object", (int)(itemEmpty.x + 10.0f), (int)(itemEmpty.y + 6.0f), 12,
                 (addMainHoverItem == 0) ? style->buttonTextHover : style->buttonText);
        DrawText("Camera", (int)(itemCamera.x + 10.0f), (int)(itemCamera.y + 6.0f), 12,
                 (addMainHoverItem == 1) ? style->buttonTextHover : style->buttonText);
        DrawText("Geometric Shapes", (int)(itemGeometry.x + 10.0f), (int)(itemGeometry.y + 6.0f), 12,
                 (addMainHoverItem == 2 || addShapesSubmenuOpen) ? style->buttonTextHover : style->buttonText);
        DrawText(">", (int)(itemGeometry.x + itemGeometry.width - 18.0f), (int)(itemGeometry.y + 6.0f), 12,
                 (addMainHoverItem == 2 || addShapesSubmenuOpen) ? style->buttonTextHover : style->buttonText);

        if (addShapesSubmenuOpen)
        {
            DrawRectangleRec(addShapesRect, style->buttonBg);
            DrawRectangleLinesEx(addShapesRect, 1.0f, style->panelBorder);
            for (int i = 0; i < addShapeCount; i++)
            {
                Rectangle item = {addShapesRect.x, addShapesRect.y + i * addItemH, addShapesRect.width, addItemH};
                bool hover = (i == addShapeHoverItem);
                DrawRectangleRec(item, hover ? TopBarMenuHoverColor() : style->buttonBg);
                if (hover)
                    DrawRectangleLinesEx(item, 1.0f, TopBarMenuBorderColor());
                DrawText(addPrimitiveLabels[i], (int)(item.x + 10.0f), (int)(item.y + 6.0f), 12,
                         hover ? style->buttonTextHover : style->buttonText);
            }
        }
    }

    // Visualizacao / Navegacao / Play / Stop / Restart
    UIButtonConfig baseCfg = {0};
    const UIStyle *uiStyle = GetUIStyle();
    Rectangle areaWireframe = GetTopBarWireframeButtonRect();
    DrawWireframeToggleIcon(areaWireframe, viewportWireframeHover, viewportWireframeMode);

    barX += 80.0f;
    if (playModeActive)
    {
        Rectangle areaNavigate = {barX, 4.0f, 84.0f, 16.0f};
        baseCfg.fontSize = 12;
        baseCfg.padding = 6;
        baseCfg.centerText = true;
        baseCfg.textColor = WHITE;
        baseCfg.textHoverColor = WHITE;
        baseCfg.bgColor = uiStyle->buttonBg;
        baseCfg.bgHoverColor = uiStyle->itemHover;
        baseCfg.borderColor = uiStyle->buttonBorder;
        baseCfg.borderHoverColor = uiStyle->accent;
        baseCfg.borderThickness = 1.0f;
        UIButtonConfig navigateCfg = baseCfg;
        Color navigateColor = viewportNavigateMode ? (Color){88, 192, 224, 255} : style->textSecondary;
        navigateCfg.textColor = navigateColor;
        navigateCfg.textHoverColor = navigateColor;
        navigateCfg.borderColor = navigateColor;
        navigateCfg.borderHoverColor = navigateColor;
        UIButtonDraw(areaNavigate, "Navegar", nullptr, &navigateCfg, navigateHover);
        barX += 94.0f;
    }

    const char *playLabel = playModeActive ? (playPaused ? "Resume" : "Pause") : "Play";
    Color playColor = playPaused ? (Color){80, 160, 220, 255} : (playModeActive ? (Color){220, 110, 80, 255} : (Color){80, 200, 120, 255});

    Rectangle areaPlay = {barX, 4.0f, 60.0f, 16.0f};
    baseCfg.fontSize = 12;
    baseCfg.padding = 6;
    baseCfg.centerText = true;
    baseCfg.textColor = WHITE;
    baseCfg.textHoverColor = WHITE;
    baseCfg.bgColor = uiStyle->buttonBg;
    baseCfg.bgHoverColor = uiStyle->itemHover;
    baseCfg.borderColor = uiStyle->buttonBorder;
    baseCfg.borderHoverColor = uiStyle->accent;
    baseCfg.borderThickness = 1.0f;
    UIButtonConfig playCfg = baseCfg;
    playCfg.textColor = playColor;
    playCfg.textHoverColor = playColor;
    playCfg.borderColor = playColor;
    playCfg.borderHoverColor = playColor;
    UIButtonDraw(areaPlay, playLabel, nullptr, &playCfg, playHover);

    if (playModeActive)
    {
        Color stopColor = (Color){210, 90, 80, 255};
        Color restartColor = (Color){90, 170, 220, 255};

        Rectangle areaStop = {barX + 70.0f, 4.0f, 60.0f, 16.0f};
        Rectangle areaRestart = {barX + 140.0f, 4.0f, 70.0f, 16.0f};

        UIButtonConfig stopCfg = baseCfg;
        stopCfg.textColor = stopColor;
        stopCfg.textHoverColor = stopColor;
        stopCfg.borderColor = stopColor;
        stopCfg.borderHoverColor = stopColor;
        UIButtonDraw(areaStop, "Stop", nullptr, &stopCfg, stopHover);

        UIButtonConfig restartCfg = baseCfg;
        restartCfg.textColor = restartColor;
        restartCfg.textHoverColor = restartColor;
        restartCfg.borderColor = restartColor;
        restartCfg.borderHoverColor = restartColor;
        UIButtonDraw(areaRestart, "Restart", nullptr, &restartCfg, restartHover);
    }

}

bool IsPlayModeActive(void)
{
    return playModeActive;
}

void SetPlayModeActive(bool active)
{
    playModeActive = active;
    if (!playModeActive)
    {
        playPaused = false;
        viewportNavigateMode = false;
    }
}

bool IsPlayPaused(void)
{
    return playPaused;
}

void SetPlayPaused(bool paused)
{
    playPaused = paused;
    if (playPaused && !playModeActive)
        playPaused = false;
}

bool ConsumePlayStopRequested(void)
{
    bool v = playStopRequested;
    playStopRequested = false;
    return v;
}

bool ConsumePlayRestartRequested(void)
{
    bool v = playRestartRequested;
    playRestartRequested = false;
    return v;
}

bool IsTopBarMenuOpen(void)
{
    return addMenuOpen;
}

bool IsViewportNavigateModeActive(void)
{
    return viewportNavigateMode && playModeActive;
}

bool IsViewportWireframeModeActive(void)
{
    return viewportWireframeMode;
}

void ToggleViewportWireframeMode(void)
{
    viewportWireframeMode = !viewportWireframeMode;
}

const char *GetViewportRenderModeLabel(void)
{
    return GetViewportModeName(viewportWireframeMode);
}
