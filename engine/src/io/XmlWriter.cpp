#include "io/XmlWriter.h"

#include <cstdint>
#include <cstring>

namespace mr
{

namespace
{
bool plain (uint32_t c)
{
    if (c >= 128)
        return false;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return true;
    return c != 0 && std::strchr (" .,;:-()_+=?!$#@[]/|*%~{}'\\", (int) c) != nullptr;
}

// UTF-8 을 코드 포인트로 읽는다 (잘못된 바이트는 그 바이트 값으로)
uint32_t nextCodePoint (const std::string& s, size_t& i)
{
    const auto b0 = (uint8_t) s[i++];
    if (b0 < 0x80)
        return b0;
    int extra = (b0 & 0xe0) == 0xc0 ? 1 : (b0 & 0xf0) == 0xe0 ? 2 : (b0 & 0xf8) == 0xf0 ? 3 : 0;
    uint32_t c = extra == 1 ? (b0 & 0x1f) : extra == 2 ? (b0 & 0x0f) : extra == 3 ? (b0 & 0x07) : b0;
    for (; extra > 0 && i < s.size() && ((uint8_t) s[i] & 0xc0) == 0x80; --extra)
        c = (c << 6) | ((uint8_t) s[i++] & 0x3f);
    return c;
}

void escape (std::string& out, const std::string& text, bool inAttribute)
{
    for (size_t i = 0; i < text.size();)
    {
        const uint32_t c = nextCodePoint (text, i);
        if (plain (c))
            out += (char) c;
        else if (c == '&')
            out += "&amp;";
        else if (c == '"')
            out += "&quot;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '<')
            out += "&lt;";
        else if ((c == '\n' || c == '\r') && ! inAttribute)
            out += (char) c;
        else
            out += "&#" + std::to_string (c) + ";";
    }
}
} // namespace

XmlNode& XmlNode::add (const std::string& name, const std::string& text)
{
    children_.push_back (std::make_unique<XmlNode> (name));
    auto& child = *children_.back();
    if (! text.empty())
    {
        auto node = std::make_unique<XmlNode> (std::string {});
        node->isText_ = true;
        node->text_ = text;
        child.children_.push_back (std::move (node));
    }
    return child;
}

XmlNode& XmlNode::set (const std::string& name, const std::string& value)
{
    for (auto& [n, v] : attributes_)
        if (n == name)
        {
            v = value;
            return *this;
        }
    attributes_.emplace_back (name, value);
    return *this;
}

void XmlNode::write (std::string& out, int indent) const
{
    out.append ((size_t) indent, ' ');
    if (isText_)
    {
        escape (out, text_, false);
        return;
    }
    out += '<';
    out += name_;
    int lineLength = 0;
    for (const auto& [n, v] : attributes_)
    {
        if (lineLength > 1000)
        {
            out += '\n';
            out.append ((size_t) indent + name_.size() + 1, ' ');
            lineLength = 0;
        }
        const size_t start = out.size();
        out += ' ';
        out += n;
        out += "=\"";
        escape (out, v, true);
        out += '"';
        lineLength += (int) (out.size() - start);
    }
    if (children_.empty())
    {
        out += "/>";
        return;
    }
    out += '>';
    bool lastWasText = false;
    for (const auto& child : children_)
    {
        if (child->isText_)
        {
            escape (out, child->text_, false);
            lastWasText = true;
            continue;
        }
        if (! lastWasText)
            out += '\n';
        child->write (out, lastWasText ? 0 : indent + 2);
        lastWasText = false;
    }
    if (! lastWasText)
    {
        out += '\n';
        out.append ((size_t) indent, ' ');
    }
    out += "</";
    out += name_;
    out += '>';
}

std::string XmlNode::document (const std::string& dtd) const
{
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n";
    if (! dtd.empty())
        out += dtd + "\n";
    write (out, 0);
    out += '\n';
    return out;
}

} // namespace mr
