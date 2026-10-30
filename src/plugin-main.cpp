#include "control-dock.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-control-dock", "en-US")
OBS_MODULE_AUTHOR("SiczenEXT")

namespace {
ControlDock *dockWidget = nullptr;
constexpr const char *DockId = "obs-control-dock";
}

bool obs_module_load(void)
{
    dockWidget = new ControlDock();
    if (!obs_frontend_add_dock_by_id(DockId, "Control Dock", dockWidget)) {
        delete dockWidget;
        dockWidget = nullptr;
        blog(LOG_ERROR, "[Control Dock] Could not register the OBS dock (duplicate dock ID?).");
        return false;
    }

    blog(LOG_INFO, "[Control Dock] loaded");
    return true;
}

void obs_module_unload(void)
{
    if (dockWidget) {
        dockWidget->saveControls();
        obs_frontend_remove_dock(DockId);
        dockWidget = nullptr;
    }
    blog(LOG_INFO, "[Control Dock] unloaded");
}

const char *obs_module_name(void)
{
    return "Control Dock";
}

const char *obs_module_description(void)
{
    return "A native OBS dock for launching automatic OBS actions and saved hotkeys.";
}
