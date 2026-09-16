// Godot GDExtension entry point.
//
// This lets the same overlay library be consumed two ways:
//   * Unity/BepInEx: dlopen + everyone_overlay_start()
//   * Godot 4:       GDExtensionManager.load_extension(), which calls gdextension_init
//
// godot-cpp is deliberately not used: the overlay registers no classes, so the
// raw C ABI (one exported entry point plus one struct) is all that is needed.
// The types below mirror Godot's core/extension/gdextension_interface.h and
// have been part of the stable 4.x ABI since 4.0.

#include <cstdint>

#include "debug_log.h"

extern "C" int everyone_overlay_start();
extern "C" int everyone_overlay_stop();

#if defined(_WIN32)
#define GDEXTENSION_EXPORT extern "C" __declspec(dllexport)
#else
#define GDEXTENSION_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace
{
    typedef uint8_t GDExtensionBool;
    typedef void* GDExtensionClassLibraryPtr;

    enum GDExtensionInitializationLevel
    {
        GDEXTENSION_INITIALIZATION_CORE,
        GDEXTENSION_INITIALIZATION_SERVERS,
        GDEXTENSION_INITIALIZATION_SCENE,
        GDEXTENSION_INITIALIZATION_EDITOR,
        GDEXTENSION_MAX_INITIALIZATION_LEVEL,
    };

    typedef void (*GDExtensionLevelCallback)(void* p_userdata, GDExtensionInitializationLevel p_level);

    struct GDExtensionInitialization
    {
        GDExtensionInitializationLevel minimum_initialization_level;
        void* userdata;
        GDExtensionLevelCallback initialize;
        GDExtensionLevelCallback deinitialize;
    };

    typedef void (*GDExtensionInterfaceFunctionPtr)();
    typedef GDExtensionInterfaceFunctionPtr (*GDExtensionInterfaceGetProcAddress)(const char* p_function_name);

    void GdInitialize(void* p_userdata, GDExtensionInitializationLevel p_level)
    {
        (void)p_userdata;
        if (p_level == GDEXTENSION_INITIALIZATION_SCENE)
        {
            DebugLog("[everyone-overlay] gdextension initialize (scene)");
            everyone_overlay_start();
        }
    }

    void GdDeinitialize(void* p_userdata, GDExtensionInitializationLevel p_level)
    {
        (void)p_userdata;
        if (p_level == GDEXTENSION_INITIALIZATION_SCENE)
        {
            DebugLog("[everyone-overlay] gdextension deinitialize (scene)");
            everyone_overlay_stop();
        }
    }
}

GDEXTENSION_EXPORT GDExtensionBool gdextension_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization* r_initialization)
{
    (void)p_get_proc_address;
    (void)p_library;

    DebugLog("[everyone-overlay] gdextension_init");

    r_initialization->minimum_initialization_level = GDEXTENSION_INITIALIZATION_SCENE;
    r_initialization->userdata = nullptr;
    r_initialization->initialize = &GdInitialize;
    r_initialization->deinitialize = &GdDeinitialize;
    return 1;
}

GDEXTENSION_EXPORT void gdextension_terminate(GDExtensionClassLibraryPtr p_library)
{
    (void)p_library;
    DebugLog("[everyone-overlay] gdextension_terminate");
    everyone_overlay_stop();
}
