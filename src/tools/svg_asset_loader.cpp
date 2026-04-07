#include "svg_asset_loader.h"
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

bool ResolveSvgAssetPath(const char *relativePath, char *out, size_t outSize)
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

Image LoadSvgImageAsset(const char *relativePath, int rasterSize)
{
    char path[512] = {0};
    if (!ResolveSvgAssetPath(relativePath, path, sizeof(path)))
        return (Image){0};

    int dataSize = 0;
    unsigned char *fileData = LoadFileData(path, &dataSize);
    if (!fileData || dataSize <= 0)
        return (Image){0};

    float svgWidth = (float)rasterSize;
    float svgHeight = (float)rasterSize;
    ParseSvgViewportSize(fileData, dataSize, &svgWidth, &svgHeight);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool shouldUninitialize = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        UnloadFileData(fileData);
        return (Image){0};
    }

    Image image = {0};
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
    return image;
}
#else
Image LoadSvgImageAsset(const char *relativePath, int rasterSize)
{
    (void)rasterSize;
    char path[512] = {0};
    if (!ResolveSvgAssetPath(relativePath, path, sizeof(path)))
        return (Image){0};
    return LoadImage(path);
}
#endif

Texture2D LoadSvgTextureAsset(const char *relativePath, int rasterSize)
{
    Image image = LoadSvgImageAsset(relativePath, rasterSize);
    if (!image.data || image.width <= 0 || image.height <= 0)
        return (Texture2D){0};

    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}
