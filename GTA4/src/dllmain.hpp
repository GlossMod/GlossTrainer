#pragma execution_character_set("utf-8")
#pragma once
#include <Windows.h>
#include <iostream>
#include <process.h>
#include <sstream>
#include <map>
#include <filesystem>

#include "log/Logger.hpp"
#include "kiero/kiero.h"
#include "minhook/include/MinHook.h"

#include "trainer/global.h"
#include "trainer/trainer.h"

BOOL APIENTRY DllMain(HMODULE handle, DWORD reason, LPVOID lpReserved);