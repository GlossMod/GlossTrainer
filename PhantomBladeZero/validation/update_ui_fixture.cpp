// Offline update states for the graphics fixture only. Never shipped in PBZ.
#define CheckAsync NetworkCheckAsync
#include "../src/update.cpp"
#undef CheckAsync

namespace gloss::update
{
    void CheckAsync() noexcept
    {
        wchar_t mode[32]{};
        GetEnvironmentVariableW(L"GLOSS_PBZ_UPDATE_FIXTURE", mode, static_cast<DWORD>(std::size(mode)));
        const std::wstring_view state(mode);
        if (state.empty() || state == L"network")
        {
            NetworkCheckAsync();
            return;
        }
        if (state == L"available")
            Publish({Status::UpdateAvailable, "PhantomBladeZero/v99.0.0", "发现新版本，可前往发布页下载",
                "https://github.com/GlossMod/GlossTrainer/releases/tag/PhantomBladeZero/v99.0.0"});
        else if (state == L"current")
            Publish({Status::Current, "PhantomBladeZero/v" + std::string(Version), "当前已是最新版本", {}});
        else if (state == L"checking")
            Publish({Status::Checking, {}, "正在检查更新…", {}});
        else
        {
            BeginCheck();
            RunCheck([] { return HttpFailure("WinHttpSendRequest", 12029); });
        }
        Log("VISUAL FIXTURE: offline update state applied; no network request");
    }
}
