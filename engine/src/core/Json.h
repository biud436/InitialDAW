// juce::var 로 읽은 JSON 을 경로가 붙은 진단과 함께 읽는다. 읽은 키를 기억해 모르는 키를 경고한다.
#pragma once

#include "core/Diagnostics.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mr::json
{

std::string typeName (const juce::var& v);
bool isNumber (const juce::var& v);
bool isInteger (const juce::var& v);

std::optional<double> readNumber (const juce::var& v, const std::string& path, Diagnostics& d,
                                  std::optional<double> lo = {}, std::optional<double> hi = {});
std::optional<int64_t> readInteger (const juce::var& v, const std::string& path, Diagnostics& d,
                                    std::optional<int64_t> lo = {}, std::optional<int64_t> hi = {});

class ObjectReader
{
public:
    ObjectReader (const juce::var& value, std::string path, Diagnostics& diagnostics);

    bool ok() const noexcept { return object_ != nullptr; }
    const std::string& path() const noexcept { return path_; }
    std::string pathOf (const std::string& key) const { return path_ + "." + key; }

    bool has (const std::string& key) const;
    std::vector<std::string> keys() const;

    // 키를 아는 키로 표시하고 값을 돌려준다. 없으면 nullptr.
    const juce::var* get (const std::string& key);
    void known (const std::string& key) { known_.insert (key); }

    std::optional<double> number (const std::string& key, bool required,
                                  std::optional<double> lo = {}, std::optional<double> hi = {});
    std::optional<int64_t> integer (const std::string& key, bool required,
                                    std::optional<int64_t> lo = {}, std::optional<int64_t> hi = {});
    std::optional<std::string> string (const std::string& key, bool required, bool allowEmpty = false);
    std::optional<bool> boolean (const std::string& key, bool required);
    const juce::Array<juce::var>* array (const std::string& key, bool required);
    const juce::var* object (const std::string& key, bool required);

    // 읽지 않은 키마다 경고한다. 가까운 아는 키가 있으면 제안한다.
    void warnUnknown (const std::vector<std::string>& candidates);

private:
    const juce::DynamicObject* object_ = nullptr;
    std::string path_;
    Diagnostics& diag_;
    std::set<std::string> known_;
};

std::string indexPath (const std::string& base, size_t index);

} // namespace mr::json
