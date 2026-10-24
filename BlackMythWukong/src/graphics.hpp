#pragma once

namespace gloss::graphics {
// Called once by InitializeASI, outside DllMain and after MH_Initialize.
bool InstallHooks();
}
