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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <imgui.h>
#include <InGameOverlay/RendererDetector.h>

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

    // Flat text scale factor applied to the overlay's ImGui default font
    // (13px) and to the window's corner margin. Kept simple and platform-free:
    // no screen/DPI queries, so it behaves identically everywhere and cannot
    // fail on unusual monitor setups. 1.5x was chosen as a comfortable
    // baseline; tune this constant if you want it bigger/smaller.
    constexpr float kTextScale = 1.5f;

    float OverlayScale()
    {
        return kTextScale;
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
