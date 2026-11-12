// 쓰기 전용의 작은 XML 트리. 요소는 두 칸씩 들여 쓰고, 텍스트만 가진 요소는 한 줄에 쓴다.
// 글자, 숫자, 몇몇 기호 밖의 문자는 실체 참조(&amp; &lt; &gt; &quot;)나 숫자 참조(&#45720;)로 쓴다.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mr
{

class XmlNode
{
public:
    explicit XmlNode (std::string name) : name_ (std::move (name)) {}

    // 자식 요소를 더해 돌려준다. text 가 있으면 그 글을 자식 텍스트로 넣는다.
    XmlNode& add (const std::string& name, const std::string& text = {});
    // 같은 이름이 있으면 값을 바꾸고, 없으면 끝에 더한다
    XmlNode& set (const std::string& name, const std::string& value);
    XmlNode& set (const std::string& name, int value) { return set (name, std::to_string (value)); }

    // XML 선언, dtd 줄, 이 요소를 쓴 문서 (끝에 줄바꿈)
    std::string document (const std::string& dtd) const;

private:
    void write (std::string& out, int indent) const;

    std::string name_, text_;
    bool isText_ = false;
    std::vector<std::pair<std::string, std::string>> attributes_;
    std::vector<std::unique_ptr<XmlNode>> children_;
};

} // namespace mr
