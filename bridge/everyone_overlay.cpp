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
//   void everyone_overlay_set_text_scale(float) -> scale factor for the rendered text

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#include <imgui.h>
#include <InGameOverlay/RendererDetector.h>

#include "debug_log.h"

#if defined(_WIN32)
#define EVERYONE_OVERLAY_EXPORT extern "C" __declspec(dllexport)
#else
#define EVERYONE_OVERLAY_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Forward declaration so the optional auto-start below can call the real
// exported entry point.
extern "C" int everyone_overlay_start();

namespace
{
    std::mutex g_mutex;
    // Heap-allocated so that a normal process exit (which never calls
    // everyone_overlay_stop) can't run a joinable std::thread destructor and
    // std::terminate the host. The pointer intentionally leaks on exit.
    std::thread* g_worker = nullptr;

    std::atomic<bool> g_started{ false };
    std::atomic<bool> g_stop{ false };
    std::atomic<bool> g_ready{ false };

    InGameOverlay::RendererHook_t* g_renderer = nullptr;

    bool g_visible = true;
    std::string g_text = "hello world";

    // Text scale factor, set from the C# side via everyone_overlay_set_text_scale.
    // The C# side computes it from the game window's actual monitor (not the
    // primary monitor), so it stays correct on multi-monitor setups and when
    // the resolution/display mode changes. 1.5f is the fallback baseline used
    // until (or unless) the C# side provides a value.
    std::atomic<float> g_text_scale{ 1.5f };

    float OverlayScale()
    {
        return g_text_scale.load();
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
        DebugLog("[everyone-overlay] hook state: %d", static_cast<int>(state));

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
            DebugLog("[everyone-overlay] hook ready");
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
        DebugLog("[everyone-overlay] detect: initial pass");
        InGameOverlay::DetectRenderer();
        DebugLog("[everyone-overlay] detect: initial pass done");
        InGameOverlay::StopRendererDetection();
        DebugLog("[everyone-overlay] detect: stopped initial detection");

        int loops = 0;
        while (!g_stop.load())
        {
            ++loops;
            if (loops <= 3 || loops % 25 == 0)
                DebugLog("[everyone-overlay] detect: poll %d", loops);

            if (!InGameOverlay::DetectRenderer(true))
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        DebugLog("[everyone-overlay] detect: finished (%d polls)", loops);

        return InGameOverlay::GetDetectedRenderer();
    }

    void WorkerMain()
    {
        DebugLog("[everyone-overlay] waiting for a renderer...");
        InGameOverlay::RendererHook_t* renderer = DetectRenderer();

        if (g_stop.load() || renderer == nullptr)
        {
            DebugLog("[everyone-overlay] no renderer detected");
            InGameOverlay::StopRendererDetection();
            InGameOverlay::FreeDetector();
            return;
        }

        DebugLog(
            "[everyone-overlay] renderer detected: %s (type %d)",
            renderer->GetLibraryName() != nullptr ? renderer->GetLibraryName() : "?",
            static_cast<int>(renderer->GetRendererHookType()));

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_renderer = renderer;
        }

        renderer->OverlayProc = []() { OverlayDraw(); };
        renderer->OverlayHookReady = [](InGameOverlay::OverlayHookState state) { OverlayHookReady(state); };

        InGameOverlay::ToggleKey toggleKeys[] = { InGameOverlay::ToggleKey::F2 };
        bool hookStarted = renderer->StartHook([]() { ToggleOverlay(); }, toggleKeys, 1);
        DebugLog("[everyone-overlay] StartHook returned %d", hookStarted ? 1 : 0);

        // The detector is no longer needed once we own the renderer hook.
        InGameOverlay::FreeDetector();
    }

    // Optional auto-start for injection methods that only *load* the library
    // (e.g. LD_PRELOAD into an engine with no native mod loader, like Godot).
    // Opt-in via env var so the normal Unity path, which calls
    // everyone_overlay_start() itself after dlopen, is completely unaffected.
    // Deferred a moment so the library's own static initializers are done
    // before the worker thread starts.
    struct AutoStart_t
    {
        AutoStart_t()
        {
            if (std::getenv("EVERYONE_OVERLAY_AUTOSTART") == nullptr)
                return;

            DebugLog("[everyone-overlay] autostart requested");
            std::thread([]()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                everyone_overlay_start();
            }).detach();
        }
    };

    AutoStart_t g_autoStart;
}

EVERYONE_OVERLAY_EXPORT int everyone_overlay_start()
{
    if (g_started.exchange(true))
        return 0; // already running

    g_stop = false;
    g_worker = new std::thread(WorkerMain);
    return 0;
}

EVERYONE_OVERLAY_EXPORT int everyone_overlay_stop()
{
    if (!g_started.exchange(false))
        return 0; // not running

    g_stop = true;

    // Wait for the detection/startup worker to finish before tearing down.
    if (g_worker != nullptr)
    {
        if (g_worker->joinable())
            g_worker->join();
        delete g_worker;
        g_worker = nullptr;
    }

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

EVERYONE_OVERLAY_EXPORT void everyone_overlay_set_text_scale(float scale)
{
    if (scale > 0.0f)
        g_text_scale.store(scale);
}
