#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <core/SkSurface.h>
#include <core/SkCanvas.h>
#include <core/SkPaint.h>
#include <core/SkData.h>
#include <codec/SkCodec.h>
#include <core/SkTypeface.h>
#include <core/SkFont.h>
#include <core/SkFontMgr.h>
#include <ports/SkTypeface_win.h>
#include <allocator.h>

const char wnd_class_name[] = "MyWindowClass";
constexpr size_t GRID_SIZE = 4ull << 32;  // 16Gb

sk_sp<SkData> read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    auto r = SkData::MakeUninitialized(ftell(f));
    fseek(f, 0, SEEK_SET);
    fread(r->writable_data(), 1, r->size(), f);
    fclose(f);
    return r;
}

sk_sp<SkImage> image;
sk_sp<SkTypeface> font;

void skia_draw(HDC hdc, int width, int height) {
    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;  // Negative height for top-down bitmap
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HDC memoryDC = CreateCompatibleDC(hdc);
    SelectObject(memoryDC, hBitmap);

    SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels, info.minRowBytes());

    if (surface) {
        SkCanvas* canvas = surface->getCanvas();
        canvas->clear(SK_ColorWHITE);
        SkPaint paint;
        paint.setColor(SK_ColorRED);
        canvas->drawRect(SkRect::MakeXYWH(50, 50, 200, 200), paint);
        paint.setColor(SK_ColorBLUE);
        canvas->drawCircle(300, 300, 100, paint);
        canvas->drawImage(image, 10.0, 20.0);
        canvas->drawSimpleText("SilverDOM", 9, SkTextEncoding::kUTF8, 20.0, 30.0, SkFont(font, 20.0), paint);
        SkPixmap pixmap;
        surface->peekPixels(&pixmap);
    }
    BitBlt(hdc, 0, 0, width, height, memoryDC, 0, 0, SRCCOPY);
    DeleteObject(hBitmap);
    DeleteDC(memoryDC);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);
        skia_draw(hdc, rect.right - rect.left, rect.bottom - rect.top);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    HANDLE grid_file = CreateFileA("grid-memory.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (grid_file == INVALID_HANDLE_VALUE) return -1;
    HANDLE grid_mapping = CreateFileMapping(grid_file, NULL, PAGE_READWRITE, GRID_SIZE >> 32, GRID_SIZE & 0xffffffff, NULL);
    if (!grid_mapping) {
        CloseHandle(grid_file);
        return -2;
    }
    void* grid = MapViewOfFileEx(grid_mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, GRID_SIZE, (void*)0x0000030000000000);
    if (!grid) {
        printf("Failed to map grid memory");
        CloseHandle(grid_mapping);
        CloseHandle(grid_file);
        return -3;
    }
    if (*(uint64_t*)grid == 0)
        ag_al_init(grid, GRID_SIZE);
    WNDCLASSEX wc{
        sizeof(WNDCLASSEX), 0, WndProc, 0, 0, hInstance,
        LoadIcon(NULL, IDI_APPLICATION),
        LoadCursor(NULL, IDC_ARROW),
        (HBRUSH)(COLOR_WINDOW + 1),
        NULL,
        wnd_class_name, LoadIcon(NULL, IDI_APPLICATION) };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        wnd_class_name,
        "SilverDOM",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);
    auto [itmp, unused] = SkCodec::MakeFromData(read_file("C:/Users/andre/cpp/silverdom/test.png"))->getImage();
    image = std::move(itmp);
    font = SkFontMgr_New_GDI()->matchFamilyStyle("Arial", SkFontStyle::Normal());
    // ->makeFromFile("C:/Users/andre/cpp/silverdom/NotoSerif-Bold.ttf");

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnmapViewOfFile(grid);
    CloseHandle(grid_mapping);
    CloseHandle(grid_file);
    return msg.wParam;
}