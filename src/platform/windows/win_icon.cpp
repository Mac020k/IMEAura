#include "platform/windows/win_icon.h"
#include "platform/windows/win_assets.h"

#include <d2d1_3.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace imeaura {
namespace {

std::wstring find_asset(const wchar_t* rel) { return win_find_asset(rel); }

HICON icon_from_hbitmap(HBITMAP color, HBITMAP mask) {
  ICONINFO info{};
  info.fIcon = TRUE;
  info.hbmColor = color;
  info.hbmMask = mask;
  return CreateIconIndirect(&info);
}

HICON bitmap_to_icon(HBITMAP bmp, int size) {
  HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
  if (!mask) return nullptr;
  const HICON icon = icon_from_hbitmap(bmp, mask);
  DeleteObject(mask);
  return icon;
}

HICON load_ico_file(const std::wstring& path, int size) {
  const HICON icon = static_cast<HICON>(
      LoadImageW(nullptr, path.c_str(), IMAGE_ICON, size, size, LR_LOADFROMFILE | LR_DEFAULTCOLOR));
  return icon;
}

HICON load_svg_icon(const std::wstring& path, int size) {
  ComPtr<IWICImagingFactory> wic;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
    return nullptr;
  }

  ComPtr<ID2D1Factory6> factory;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory6), nullptr,
                                reinterpret_cast<void**>(factory.GetAddressOf())))) {
    return nullptr;
  }

  ComPtr<IWICBitmap> wic_bitmap;
  if (FAILED(wic->CreateBitmap(static_cast<UINT>(size), static_cast<UINT>(size), GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapCacheOnLoad, &wic_bitmap))) {
    return nullptr;
  }

  const D2D1_RENDER_TARGET_PROPERTIES rt_props =
      D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                   D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

  ComPtr<ID2D1RenderTarget> rt;
  if (FAILED(factory->CreateWicBitmapRenderTarget(wic_bitmap.Get(), rt_props, &rt))) return nullptr;

  ComPtr<ID2D1DeviceContext5> dc;
  if (FAILED(rt.As(&dc))) return nullptr;

  ComPtr<IWICStream> stream;
  if (FAILED(wic->CreateStream(&stream))) return nullptr;
  if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_READ))) return nullptr;

  ComPtr<ID2D1SvgDocument> svg;
  if (FAILED(dc->CreateSvgDocument(stream.Get(), D2D1::SizeF(static_cast<float>(size), static_cast<float>(size)),
                                   &svg))) {
    return nullptr;
  }

  dc->BeginDraw();
  dc->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
  dc->DrawSvgDocument(svg.Get());
  if (FAILED(dc->EndDraw())) return nullptr;

  ComPtr<IWICFormatConverter> converter;
  if (FAILED(wic->CreateFormatConverter(&converter))) return nullptr;
  if (FAILED(converter->Initialize(wic_bitmap.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom))) {
    return nullptr;
  }

  std::vector<uint8_t> pixels(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
  if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(size * 4), static_cast<UINT>(pixels.size()),
                                   pixels.data()))) {
    return nullptr;
  }

  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = size;
  bi.bmiHeader.biHeight = -size;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (!color || !bits) return nullptr;

  memcpy(bits, pixels.data(), pixels.size());
  return bitmap_to_icon(color, size);
}

}  // namespace

HICON win_load_app_icon(int size_px) {
  if (size_px <= 0) size_px = 32;

  if (const auto ico = find_asset(L"img/icon.ico"); !ico.empty()) {
    if (HICON icon = load_ico_file(ico, size_px)) return icon;
  }

  if (const auto svg = find_asset(L"img/icon.svg"); !svg.empty()) {
    if (HICON icon = load_svg_icon(svg, size_px)) return icon;
  }

  return LoadIconW(nullptr, IDI_APPLICATION);
}

void win_set_window_icons(HWND hwnd) {
  if (!hwnd) return;
  if (HICON icon_large = win_load_app_icon(GetSystemMetrics(SM_CXICON))) {
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_large));
  }
  if (HICON icon_small = win_load_app_icon(GetSystemMetrics(SM_CXSMICON))) {
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_small));
  }
}

}  // namespace imeaura
