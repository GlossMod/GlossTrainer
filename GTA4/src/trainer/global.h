#pragma once

#include "../dllmain.hpp"

namespace global
{

    enum tab
    {
        base,
        settings,
        tab_3,
        tab_4,
    };

    static bool showWindow = true;
    static tab trainerTab = tab::base;

    // 游戏设置
    static int gameWindowWidth = 1920;
    static int gameWindowHeight = 1080;

}