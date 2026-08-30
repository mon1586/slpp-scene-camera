#pragma once

namespace ssc::controller
{
    class CameraHook
    {
    public:
        static void Install();

    private:
        static void Thunk(RE::PlayerCamera* a_camera);
        static inline REL::Relocation<decltype(Thunk)> original_;
    };
}

