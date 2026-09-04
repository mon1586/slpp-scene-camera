#include "runtime/CameraInput.h"

#include <REX/W32/DINPUT.h>

namespace ssc::runtime
{
    CameraInput* CameraInput::GetSingleton() noexcept
    {
        static CameraInput singleton;
        return std::addressof(singleton);
    }

    void CameraInput::Configure(IRuntimeClient& a_client) noexcept
    {
        client_ = std::addressof(a_client);
    }

    bool CameraInput::Register() noexcept
    {
        try {
            auto* input = RE::BSInputDeviceManager::GetSingleton();
            if (!input) {
                logger::error("Cannot register camera input: input manager is unavailable");
                return false;
            }
            input->AddEventSink(GetSingleton());
            logger::info("Camera A/D input observer registered");
            return true;
        } catch (...) {
            try {
                logger::error("Camera input registration failed");
            } catch (...) {
            }
            return false;
        }
    }

    RE::BSEventNotifyControl CameraInput::ProcessEvent(
        RE::InputEvent* const* a_events,
        RE::BSTEventSource<RE::InputEvent*>*)
    {
        if (!a_events) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* client = client_;
        if (!client) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto* event = *a_events; event; event = event->next) {
            if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton ||
                event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                continue;
            }
            const auto* button = event->AsButtonEvent();
            if (!button || !button->IsDown()) {
                continue;
            }

            switch (button->GetIDCode()) {
            case REX::W32::DIK_A:
                client->RequestPresetStep(-1);
                break;
            case REX::W32::DIK_D:
                client->RequestPresetStep(1);
                break;
            default:
                break;
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
}
