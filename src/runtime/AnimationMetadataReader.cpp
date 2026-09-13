#include "runtime/AnimationMetadataReader.h"

namespace ssc::runtime
{
    namespace
    {
        using Reply = MetadataReply;
        std::atomic_size_t pendingCallbacks{ 0 };
        constexpr std::size_t kMaximumCallbacks = 128;
        // VM callbacks only copy their return values into a private mailbox.
        // They neither access camera state nor dispatch further work. Dropping
        // an update therefore makes late callbacks harmless, including on load.
        class Callback final : public RE::BSScript::IStackCallbackFunctor
        {
        public:
            Callback(std::shared_ptr<Reply> a_reply, bool a_tags) :
                reply_(std::move(a_reply)), tags_(a_tags) { ++pendingCallbacks; }
            ~Callback() override { --pendingCallbacks; }

            void operator()(RE::BSScript::Variable a_result) override
            {
                std::scoped_lock lock{ reply_->mutex };
                try {
                    reply_->status = "unexpected_type";
                    if (!tags_ && a_result.IsString()) {
                        reply_->text = a_result.GetString();
                        reply_->status = "ok";
                    } else if (tags_ && a_result.IsArray()) {
                        const auto array = a_result.GetArray();
                        reply_->status = "none_array";
                        if (array) {
                            reply_->status = "ok";
                            for (const auto& item : *array) {
                                if (!item.IsString()) {
                                    reply_->status = "unexpected_element_type";
                                    reply_->tags.clear();
                                    break;
                                }
                                reply_->tags.emplace_back(item.GetString());
                            }
                        }
                    }
                } catch (...) {
                    reply_->status = "callback_exception";
                }
                reply_->complete = true;
            }

            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
            bool CanSave() const override { return false; }

        private:
            std::shared_ptr<Reply> reply_;
            bool tags_;
        };

        void Fail(const std::shared_ptr<Reply>& a_reply, const char* a_status)
        {
            std::scoped_lock lock{ a_reply->mutex };
            a_reply->status = a_status;
            a_reply->complete = true;
        }

        std::shared_ptr<Reply> ReadActiveScene(const SceneKey& a_key)
        {
            auto reply = std::make_shared<Reply>();
            if (pendingCallbacks.load(std::memory_order_relaxed) >= kMaximumCallbacks) {
                Fail(reply, "request_capacity");
                return reply;
            }
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_key.sourceID);
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            if (!vm || !quest || !policy) {
                Fail(reply, "vm_or_quest_unavailable");
                return reply;
            }
            const auto handle = policy->GetHandleForObject(quest->GetFormType(), quest);
            RE::BSTSmartPointer<RE::BSScript::Object> object;
            if (!vm->FindBoundObject(handle, "sslThreadController", object) || !object) {
                Fail(reply, "sslThreadController_not_bound");
                return reply;
            }
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{
                new Callback(reply, false) };
            if (!vm->DispatchMethodCall(object, "GetActiveScene", RE::MakeFunctionArguments(), callback)) {
                Fail(reply, "dispatch_failed");
            }
            return reply;
        }

        std::shared_ptr<Reply> ReadMetadata(const std::string& a_id, bool a_tags)
        {
            auto reply = std::make_shared<Reply>();
            if (pendingCallbacks.load(std::memory_order_relaxed) >= kMaximumCallbacks) {
                Fail(reply, "request_capacity");
                return reply;
            }
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                Fail(reply, "vm_unavailable");
                return reply;
            }
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{
                new Callback(reply, a_tags) };
            auto id = RE::BSFixedString{ a_id };
            if (!vm->DispatchStaticCall("SexLabRegistry", a_tags ? "GetSceneTags" : "GetSceneName",
                    RE::MakeFunctionArguments(std::move(id)), callback)) {
                Fail(reply, "dispatch_failed");
            }
            return reply;
        }

    }

    void ConfigureAnimationMetadataReader(AnimationUpdateCoordinator& a_coordinator)
    {
        a_coordinator.Configure(ReadActiveScene, ReadMetadata);
    }
}
