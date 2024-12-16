#include "SDL2/SDL.h"
#include "SDL2/SDL_opengl.h"
#include "skia/core/SkCanvas.h"
#include "skia/core/SkColorSpace.h"
#include "skia/core/SkFont.h"
#include "skia/core/SkFontMgr.h"
#include "skia/core/SkSurface.h"
#include "skia/private/base/SkTArray.h"
#include "skia/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "skia/gpu/ganesh/gl/GrGLDirectContext.h"
#include "skia/gpu/ganesh/SkSurfaceGanesh.h"
#include "skia/gpu/GrRecordingContext.h"
#include "skia/gpu/GrDirectContext.h"
#include "skia/gpu/GrBackendSurface.h"
#include "skia/src/base/SkRandom.h"
#include "skia/src/gpu/ganesh/gl/GrGLUtil.h"
#include "skia/codec/SkCodec.h"


#if defined(SK_BUILD_FOR_ANDROID)
#   include <GLES/gl.h>
#elif defined(SK_BUILD_FOR_UNIX)
#   include <GL/gl.h>
#elif defined(SK_BUILD_FOR_MAC)
#   include <OpenGL/gl.h>
#elif defined(SK_BUILD_FOR_IOS)
#   include <OpenGLES/ES2/gl.h>
#endif

#ifdef __linux__
#include "skia/ports/SkFontConfigInterface.h"
#include "skia/ports/SkFontMgr_FontConfigInterface.h"
#elif __APPLE__
#include "skia/ports/SkFontMgr_mac_ct.h"
#elif WIN32
#include <skia/ports/SkTypeface_win.h>
#endif

bool gCheckErrorGL = false;
bool gLogCallsGL = false;

sk_sp<SkData> read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    auto r = SkData::MakeUninitialized(ftell(f));
    fseek(f, 0, SEEK_SET);
    fread(r->writable_data(), 1, r->size(), f);
    fclose(f);
    return r;
}

sk_sp<SkImage> create_cpu_image(SkImageInfo& img_info, std::function<void(SkCanvas*)> painter) {
    sk_sp<SkSurface> surf = SkSurfaces::Raster(img_info);
    painter(surf->getCanvas());
    return surf->makeImageSnapshot();
}

std::vector<SkRect> rects;
int window_width;
int window_height;

void handle_error() {
    const char* error = SDL_GetError();
    SkDebugf("SDL Error: %s\n", error);
    exit(-1);
}

bool handle_events() {  // return true if quits
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_MOUSEMOTION:
            if (event.motion.state == SDL_PRESSED) {
                SkRect& rect = rects.back();
                rect.fRight = event.motion.x;
                rect.fBottom = event.motion.y;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.state == SDL_PRESSED) {
                rects.push_back(SkRect::MakeLTRB(
                    SkIntToScalar(event.button.x),
                    SkIntToScalar(event.button.y),
                    SkIntToScalar(event.button.x),
                    SkIntToScalar(event.button.y)));
            }
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || event.window.event == SDL_WINDOWEVENT_RESIZED) {
                window_width = event.window.data1;
                window_height = event.window.data2;
            }
            break;
        case SDL_KEYDOWN:
        {
            SDL_Keycode key = event.key.keysym.sym;
            if (key == SDLK_ESCAPE) {
                return true;
            }
            break;
        }
        case SDL_QUIT:
            return true;
            break;
        default:
            break;
        }
    }
    return false;
}

SkPath create_polygon(int vertex_count, SkScalar size) {
    SkPoint pt {0, size};
    SkMatrix rot;
    rot.setRotate(SkIntToScalar(360) / vertex_count);
    SkPath result;
    result.moveTo(pt);
    for (int i = 1; i < vertex_count; ++i) {
        SkPoint dst;
        rot.mapPoints(&dst, &pt, 1);
        pt = dst;
        result.lineTo(pt);
    }
    result.setFillType(SkPathFillType::kEvenOdd);
    result.close();
    return result;
}

#if defined(SK_BUILD_FOR_ANDROID)
int SDL_main(int argc, char** argv) {
#else
int main(int argc, char** argv) {
#endif
    uint32_t window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

#if defined(SK_BUILD_FOR_ANDROID) || defined(SK_BUILD_FOR_IOS)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    windowFlags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI;
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);   // TODO: make configurable
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8); // Skia requirement
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) handle_error();
    SDL_DisplayMode sdl_display_mode;
    if (SDL_GetDesktopDisplayMode(0, &sdl_display_mode) != 0) handle_error();
    SDL_Window* window = SDL_CreateWindow(
        "SilverDOM",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        sdl_display_mode.w,
        sdl_display_mode.h,
        window_flags);
    if (!window) handle_error();
    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) handle_error();

    if (SDL_GL_MakeCurrent(window, gl_context) != 0) handle_error();

    uint32_t window_format = SDL_GetWindowPixelFormat(window);
    int context_type;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &context_type);

    SDL_GL_GetDrawableSize(window, &window_width, &window_height);
    {
        auto interface = GrGLMakeNativeInterface();
        sk_sp<GrDirectContext> gr_context = GrDirectContexts::MakeGL(interface);
        SkASSERT(gr_context);
        GrGLFramebufferInfo fb_info;
        GR_GL_GetIntegerv(interface.get(), GR_GL_FRAMEBUFFER_BINDING, (GrGLint*)&fb_info.fFBOID);
        fb_info.fFormat = window_format == SDL_PIXELFORMAT_RGBA8888 || context_type != SDL_GL_CONTEXT_PROFILE_ES
                ? GR_GL_RGBA8
                : GR_GL_BGRA8;
        auto render_target = GrBackendRenderTargets::MakeGL(
            window_width, window_height,
            0, // MSAA Sample Count
            8, // Stencil Bits
            fb_info);
        SkSurfaceProps fb_props(SkSurfaceProps::kUseDeviceIndependentFonts_Flag, kUnknown_SkPixelGeometry);
        sk_sp<SkSurface> fb_surface = SkSurfaces::WrapBackendRenderTarget(
            (GrRecordingContext*)gr_context.get(),
            render_target,
            kBottomLeft_GrSurfaceOrigin,
            window_format == SDL_PIXELFORMAT_RGBA8888 ? kRGBA_8888_SkColorType : kBGRA_8888_SkColorType,
            nullptr,
            &fb_props);
        SkCanvas* canvas = fb_surface->getCanvas();
        canvas->scale(
            (float)window_width / sdl_display_mode.w,
            (float)window_height / sdl_display_mode.h);
        SkPaint paint;
        auto hexagon = create_polygon(6, 50);
        int rotation = 0;
#ifdef __linux__
        sk_sp<SkTypeface> typeface = SkFontMgr_New_FCI(SkFontConfigInterface::RefGlobal())->legacyMakeTypeface("", SkFontStyle());
#elif __APPLE__
        sk_sp<SkTypeface> typeface = SkFontMgr_New_CoreText(nullptr)->legacyMakeTypeface("", SkFontStyle());
#else
        sk_sp<SkTypeface> typeface = SkFontMgr_New_GDI()->matchFamilyStyle("Arial", SkFontStyle::Normal());
#endif
        SkFont font(typeface, 24);
        auto [itmp, unused] = SkCodec::MakeFromData(read_file("C:/Users/andre/cpp/silverdom/test.png"))->getImage();
        sk_sp<SkImage> my_image = std::move(itmp);

        for (;;) {
            SkRandom rand;
            canvas->clear(SK_ColorWHITE);
            if (handle_events()) break;

            for (auto& r : rects) {
                paint.setColor(rand.nextU() | 0x80808080);
                canvas->drawRoundRect(r, 5.0f, 5.0f, paint);
            }
            paint.setColor(SK_ColorBLACK);
            canvas->drawImage(my_image, 10.0, 20.0);
            canvas->drawString("Silverdom", 100.0f, 100.0f, font, paint);
            canvas->save();
            canvas->translate((sdl_display_mode.w - 100) / 2.0, (sdl_display_mode.h - 100) / 2.0);
            canvas->rotate(rotation++);
            paint.setColor(SK_ColorBLACK);
            canvas->drawPath(hexagon, paint);
            canvas->restore();
            if (auto as_direct_context = GrAsDirectContext(canvas->recordingContext()))
                as_direct_context->flushAndSubmit();
            SDL_GL_SwapWindow(window);
            SDL_Delay(1000 / 60);
        }
    }
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}