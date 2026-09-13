#pragma once
#include "runtime/AnimationMetadataStore.h"
#include <regex>
#include <optional>
#include <locale>

namespace ssc::runtime
{
    class PresetFilterEvaluator
    {
    public:
        PresetFilterEvaluator(const std::string& a_name, const std::string& a_tag)
        {
            try {
                Compile(a_name, name_);
                Compile(a_tag, tag_);
            } catch (const std::exception& error) {
                error_ = error.what();
            }
        }

        const std::string& Error() const noexcept { return error_; }
        bool Matches(const AnimationMetadata& a_metadata) const
        {
            if (!error_.empty()) { return false; }
            if (!name_ && !tag_) { return true; }
            if (!a_metadata.known) { return false; }
            try {
                if (name_ && !std::regex_search(a_metadata.name, *name_)) { return false; }
                if (!tag_) { return true; }
                std::string tags;
                for (std::size_t index = 0; index < a_metadata.tags.size(); ++index) {
                    if (index != 0) { tags += ' '; }
                    tags += a_metadata.tags[index];
                }
                return std::regex_search(tags, *tag_);
            } catch (const std::regex_error&) { return false; }
            return false;
        }

    private:
        static void Compile(const std::string& a_text, std::optional<std::regex>& a_regex)
        {
            if (a_text.size() > 511 || a_text.find('\0') != std::string::npos) {
                throw std::invalid_argument("Regex must be at most 511 bytes and contain no NUL");
            }
            if (a_text.empty()) { return; }
            a_regex.emplace();
            a_regex->imbue(std::locale::classic());
            a_regex->assign(a_text, std::regex::ECMAScript | std::regex::icase);
        }
        std::optional<std::regex> name_;
        std::optional<std::regex> tag_;
        std::string error_;
    };
}
