#include "core/Json.h"

#include <cmath>

namespace mr::json
{

std::string typeName (const juce::var& v)
{
    if (v.isVoid() || v.isUndefined())
        return "null";
    if (v.isBool())
        return "boolean";
    if (v.isInt() || v.isInt64())
        return "integer";
    if (v.isDouble())
        return "number";
    if (v.isString())
        return "string";
    if (v.isArray())
        return "array";
    if (v.isObject())
        return "object";
    return "unknown";
}

bool isNumber (const juce::var& v)
{
    return v.isInt() || v.isInt64() || v.isDouble();
}

bool isInteger (const juce::var& v)
{
    if (v.isInt() || v.isInt64())
        return true;
    if (v.isDouble())
    {
        const double d = (double) v;
        return std::isfinite (d) && std::floor (d) == d && std::abs (d) < 9.0e15;
    }
    return false;
}

std::optional<double> readNumber (const juce::var& v, const std::string& path, Diagnostics& d,
                                  std::optional<double> lo, std::optional<double> hi)
{
    if (! isNumber (v))
    {
        d.error (path, "expected a number, got " + typeName (v));
        return std::nullopt;
    }
    const double x = (double) v;
    if (! std::isfinite (x))
    {
        d.error (path, "number must be finite");
        return std::nullopt;
    }
    if ((lo && x < *lo) || (hi && x > *hi))
    {
        std::string range = (lo ? formatNumber (*lo) : std::string ("-inf")) + " .. " + (hi ? formatNumber (*hi) : std::string ("inf"));
        d.error (path, "value " + formatNumber (x) + " is out of range " + range);
        return std::nullopt;
    }
    return x;
}

std::optional<int64_t> readInteger (const juce::var& v, const std::string& path, Diagnostics& d,
                                    std::optional<int64_t> lo, std::optional<int64_t> hi)
{
    if (! isInteger (v))
    {
        d.error (path, "expected an integer, got " + (isNumber (v) ? std::string ("a fractional number ") + formatNumber ((double) v) : typeName (v)));
        return std::nullopt;
    }
    const int64_t x = v.isDouble() ? (int64_t) (double) v : (int64_t) v;
    if ((lo && x < *lo) || (hi && x > *hi))
    {
        std::string range = (lo ? std::to_string (*lo) : std::string ("-inf")) + " .. " + (hi ? std::to_string (*hi) : std::string ("inf"));
        d.error (path, "value " + std::to_string (x) + " is out of range " + range);
        return std::nullopt;
    }
    return x;
}

ObjectReader::ObjectReader (const juce::var& value, std::string path, Diagnostics& diagnostics)
    : path_ (std::move (path)), diag_ (diagnostics)
{
    if (auto* o = value.getDynamicObject(); o != nullptr && ! value.isArray())
        object_ = o;
    else
        diag_.error (path_, "expected an object, got " + typeName (value));
}

bool ObjectReader::has (const std::string& key) const
{
    return object_ != nullptr && key.size() > 0 && object_->hasProperty (juce::Identifier (key));
}

std::vector<std::string> ObjectReader::keys() const
{
    std::vector<std::string> out;
    if (object_ != nullptr)
        for (const auto& p : object_->getProperties())
            out.push_back (p.name.toString().toStdString());
    return out;
}

const juce::var* ObjectReader::get (const std::string& key)
{
    known_.insert (key);
    if (! has (key))
        return nullptr;
    return object_->getProperties().getVarPointer (juce::Identifier (key));
}

std::optional<double> ObjectReader::number (const std::string& key, bool required, std::optional<double> lo, std::optional<double> hi)
{
    const auto* v = get (key);
    if (v == nullptr)
    {
        if (required && ok())
            diag_.error (pathOf (key), "required number is missing");
        return std::nullopt;
    }
    return readNumber (*v, pathOf (key), diag_, lo, hi);
}

std::optional<int64_t> ObjectReader::integer (const std::string& key, bool required, std::optional<int64_t> lo, std::optional<int64_t> hi)
{
    const auto* v = get (key);
    if (v == nullptr)
    {
        if (required && ok())
            diag_.error (pathOf (key), "required integer is missing");
        return std::nullopt;
    }
    return readInteger (*v, pathOf (key), diag_, lo, hi);
}

std::optional<std::string> ObjectReader::string (const std::string& key, bool required, bool allowEmpty)
{
    const auto* v = get (key);
    if (v == nullptr)
    {
        if (required && ok())
            diag_.error (pathOf (key), "required string is missing");
        return std::nullopt;
    }
    if (! v->isString())
    {
        diag_.error (pathOf (key), "expected a string, got " + typeName (*v));
        return std::nullopt;
    }
    auto s = v->toString().toStdString();
    if (s.empty() && ! allowEmpty)
    {
        diag_.error (pathOf (key), "string must not be empty");
        return std::nullopt;
    }
    return s;
}

std::optional<bool> ObjectReader::boolean (const std::string& key, bool required)
{
    const auto* v = get (key);
    if (v == nullptr)
    {
        if (required && ok())
            diag_.error (pathOf (key), "required boolean is missing");
        return std::nullopt;
    }
    if (! v->isBool())
    {
        diag_.error (pathOf (key), "expected true or false, got " + typeName (*v));
        return std::nullopt;
    }
    return (bool) *v;
}

const juce::Array<juce::var>* ObjectReader::array (const std::string& key, bool required)
{
    const auto* v = get (key);
    if (v == nullptr)
    {
        if (required && ok())
            diag_.error (pathOf (key), "required array is missing");
        return nullptr;
    }
    if (! v->isArray())
    {
        diag_.error (pathOf (key), "expected an array, got " + typeName (*v));
        return nullptr;
    }
    return v->getArray();
}

const juce::var* ObjectReader::object (const std::string& key, bool required)
{
    const auto* v = get (key);
    if (v == nullptr)
    {
        if (required && ok())
            diag_.error (pathOf (key), "required object is missing");
        return nullptr;
    }
    if (! v->isObject() || v->isArray())
    {
        diag_.error (pathOf (key), "expected an object, got " + typeName (*v));
        return nullptr;
    }
    return v;
}

void ObjectReader::warnUnknown (const std::vector<std::string>& candidates)
{
    if (object_ == nullptr)
        return;
    for (const auto& key : keys())
    {
        if (known_.count (key) > 0)
            continue;
        const auto suggestion = closestMatch (key, candidates);
        diag_.warning (pathOf (key), "unknown field '" + key + "' is ignored",
                       suggestion.empty() ? std::string {} : "did you mean '" + suggestion + "'?");
    }
}

std::string indexPath (const std::string& base, size_t index)
{
    return base + "[" + std::to_string (index) + "]";
}

} // namespace mr::json
