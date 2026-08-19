// Everyone overlay bridge.
//
// A tiny native shared library that ties the BepInEx mod to
// Nemirtingas/ingame_overlay. The C# side loads this library and calls
// `everyone_overlay_start()`, which spawns a worker thread that detects the
// game's renderer, hooks it, and draws a single ImGui text label.
//
// Exposed C API:
//   int  everyone_overlay_start(void)          -> 0 on success (idempotent)
//   int  everyone_overlay_stop(void)           -> 0 on success
//   int  everyone_overlay_is_ready(void)       -> 1 once the overlay is hooked
//   void everyone_overlay_set_text(const char*) -> change the rendered text

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <imgui.h>
#include <InGameOverlay/RendererDetector.h>

// X11 headers must come after InGameOverlay's headers: Xlib defines generic
// macros (None, Status, ...) that collide with InGameOverlay's enums.
#if defined(__linux__) && !defined(__APPLE__)
#include <X11/Xlib.h>
#undef Status
#endif

#if defined(_WIN32)
#define EVERYONE_OVERLAY_EXPORT extern "C" __declspec(dllexport)
#else
#define EVERYONE_OVERLAY_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace
{
    std::mutex g_mutex;
    std::thread g_worker;

    std::atomic<bool> g_started{ false };
    std::atomic<bool> g_stop{ false };
    std::atomic<bool> g_ready{ false };

    InGameOverlay::RendererHook_t* g_renderer = nullptr;

    bool g_visible = true;
    std::string g_text = "hello world";

    // The overlay text is designed against a 1080p reference screen. Scaling
    // by the *physical screen height* (in the game process's own coordinate
    // space) keeps the text a constant physical size on screen, regardless of
    // the game resolution, windowed vs fullscreen, or DPI awareness:
    //
    //   - High-DPI displays (4K, 1440p) get a bigger scale, so the text is
    //     readable even when the game runs at 1080p on them (previously the
    //     text stayed a fixed 13px sliver in those cases).
    //   - 1080p displays get scale 1.0, so the text looks exactly as before
    //     (never "too big" on low-DPI setups).
    //   - In exclusive fullscreen the display mode changes, so the reported
    //     screen height matches the back buffer and the scale compensates for
    //     the upscale exactly (constant physical size at any resolution).
    //
    // GetSystemMetrics(SM_CYSCREEN) is returned in the process's coordinate
    // space: physical pixels for DPI-aware games, virtualized pixels for
    // DPI-unaware games (which Windows bitmap-stretches by the same factor),
    // so no DPI-awareness handling is needed here.
    float GetScreenHeightPx()
    {
#if defined(_WIN32)
        return static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
#elif defined(__linux__) && !defined(__APPLE__)
        static Display* display = XOpenDisplay(nullptr);
        if (display != nullptr)
            return static_cast<float>(DisplayHeight(display, DefaultScreen(display)));
        return 0.0f; // e.g. Wayland without XWayland; caller falls back to the viewport height
#else
        return 0.0f;
#endif
    }

    float OverlayScale()
    {
        const float kReferenceHeight = 1080.0f;
        float screenHeight = GetScreenHeightPx();
        if (screenHeight <= 0.0f)
            screenHeight = ImGui::GetIO().DisplaySize.y; // fallback: scale by viewport instead
        if (screenHeight <= 0.0f)
            return 1.0f;
        return std::max(1.0f, screenHeight / kReferenceHeight);
    }

    void OverlayDraw()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_visible)
            return;

        float scale = OverlayScale();
        ImGui::SetNextWindowPos(ImVec2{ 16.0f * scale, 16.0f * scale });
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin(
            "Everyone",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
        ImGui::TextUnformatted(g_text.c_str());
        ImGui::PopFont();
        ImGui::End();
    }

    void OverlayHookReady(InGameOverlay::OverlayHookState state)
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (state == InGameOverlay::OverlayHookState::Ready)
        {
            // Passive overlay: the game keeps receiving input, the overlay does
            // not react to it. This keeps "hello world" purely visual.
            if (g_renderer != nullptr)
            {
                g_renderer->HideAppInputs(false);
                g_renderer->HideOverlayInputs(true);
            }
            g_ready = true;
        }
        else if (state == InGameOverlay::OverlayHookState::Removing)
        {
            g_ready = false;
        }
    }

    void ToggleOverlay()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_visible = !g_visible;

        // Keep the overlay passive regardless of visibility.
        if (g_renderer != nullptr)
        {
            g_renderer->HideAppInputs(false);
            g_renderer->HideOverlayInputs(true);
        }
    }

    InGameOverlay::RendererHook_t* DetectRenderer()
    {
        // Kick off detection, then poll until the game actually renders a frame
        // through the detected renderer (which is when detection completes).
        InGameOverlay::DetectRenderer();
        InGameOverlay::StopRendererDetection();

        while (!g_stop.load())
        {
            if (!InGameOverlay::DetectRenderer(true))
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        return InGameOverlay::GetDetectedRenderer();
    }

    void WorkerMain()
    {
        InGameOverlay::RendererHook_t* renderer = DetectRenderer();

        if (g_stop.load() || renderer == nullptr)
        {
            InGameOverlay::StopRendererDetection();
            InGameOverlay::FreeDetector();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_renderer = renderer;
        }

        renderer->OverlayProc = []() { OverlayDraw(); };
        renderer->OverlayHookReady = [](InGameOverlay::OverlayHookState state) { OverlayHookReady(state); };

        InGameOverlay::ToggleKey toggleKeys[] = { InGameOverlay::ToggleKey::F2 };
        renderer->StartHook([]() { ToggleOverlay(); }, toggleKeys, 1);

        // The detector is no longer needed once we own the renderer hook.
        InGameOverlay::FreeDetector();
    }
}

EVERYONE_OVERLAY_EXPORT int everyone_overlay_start()
{
    if (g_started.exchange(true))
        return 0; // already running

    g_stop = false;
    g_worker = std::thread(WorkerMain);
    return 0;
}

EVERYONE_OVERLAY_EXPORT int everyone_overlay_stop()
{
    if (!g_started.exchange(false))
        return 0; // not running

    g_stop = true;

    // Wait for the detection/startup worker to finish before tearing down.
    if (g_worker.joinable())
        g_worker.join();

    InGameOverlay::RendererHook_t* renderer = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        renderer = g_renderer;
        g_renderer = nullptr;
    }

    // Deleting the hook unhooks the renderer and stops invoking OverlayProc.
    delete renderer;

    g_ready = false;
    return 0;
}

EVERYONE_OVERLAY_EXPORT int everyone_overlay_is_ready()
{
    return g_ready.load() ? 1 : 0;
}

EVERYONE_OVERLAY_EXPORT void everyone_overlay_set_text(const char* text)
{
    if (text == nullptr)
        return;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_text = text;
}
