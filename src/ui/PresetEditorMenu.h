#pragma once

namespace ssc::ui
{
    class PresetEditorMenu
    {
    public:
        [[nodiscard]] static bool Register();
        static void CloseForLifecycle() noexcept;
    };
}
