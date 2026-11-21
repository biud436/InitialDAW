#include "io/Pdf.h"

#include <juce_core/juce_core.h>

#include <cstdio>
#include <stdexcept>

namespace mr
{

namespace
{

std::string number (double v) { return juce::String (v, 2).toStdString(); }

// PDF 문자열: UTF-16BE 의 16진수 (BOM FEFF 로 시작)
std::string textString (const std::string& utf8)
{
    std::string out = "<FEFF";
    const auto s = juce::String::fromUTF8 (utf8.c_str());
    const auto* p = s.toUTF16().getAddress();
    static const char* hex = "0123456789ABCDEF";
    for (; *p != 0; ++p)
    {
        const auto c = (uint16_t) *p;
        for (int shift = 12; shift >= 0; shift -= 4)
            out += hex[(c >> shift) & 0xF];
    }
    return out + ">";
}

std::vector<uint8_t> deflate (const std::vector<uint8_t>& data)
{
    juce::MemoryOutputStream out;
    {
        juce::GZIPCompressorOutputStream zip (out, 6, 0);    // 0: zlib 머리와 꼬리 (PDF 의 FlateDecode)
        zip.write (data.data(), data.size());
        zip.flush();
    }
    const auto* bytes = static_cast<const uint8_t*> (out.getData());
    return { bytes, bytes + out.getDataSize() };
}

} // namespace

std::vector<uint8_t> imagePdf (const std::vector<PdfPage>& pages, const PdfInfo& info)
{
    if (pages.empty())
        throw std::runtime_error ("PDF 에 쪽이 없습니다");
    std::vector<uint8_t> out;
    std::vector<size_t> offsets;                     // 객체 번호 1 부터
    auto put = [&] (const std::string& s) { out.insert (out.end(), s.begin(), s.end()); };
    auto begin = [&] (size_t id)
    {
        if (offsets.size() < id)
            offsets.resize (id, 0);
        offsets[id - 1] = out.size();
        put (std::to_string (id) + " 0 obj\n");
    };
    put ("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
    // 1 카탈로그, 2 쪽 목록, 3 정보, 그다음 쪽마다 쪽, 내용, 그림
    const size_t n = pages.size();
    auto pageId = [] (size_t i) { return 4 + 3 * i; };
    begin (1);
    put ("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    begin (2);
    put ("<< /Type /Pages /Kids [");
    for (size_t i = 0; i < n; ++i)
        put ((i ? " " : "") + std::to_string (pageId (i)) + " 0 R");
    put ("] /Count " + std::to_string (n) + " >>\nendobj\n");
    begin (3);
    put ("<< /Title " + textString (info.title) + " /Subject " + textString (info.subject) + " /Creator " + textString (info.creator) + " /Producer "
         + textString (info.creator) + " >>\nendobj\n");
    for (size_t i = 0; i < n; ++i)
    {
        const auto& p = pages[i];
        if (p.pixelWidth <= 0 || p.pixelHeight <= 0 || p.gray.size() != (size_t) p.pixelWidth * (size_t) p.pixelHeight)
            throw std::runtime_error ("PDF 쪽 " + std::to_string (i + 1) + " 의 그림 크기가 맞지 않습니다");
        const auto w = number (p.widthPoints), h = number (p.heightPoints);
        begin (pageId (i));
        put ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + w + " " + h + "] /Resources << /XObject << /Im0 " + std::to_string (pageId (i) + 2)
             + " 0 R >> >> /Contents " + std::to_string (pageId (i) + 1) + " 0 R >>\nendobj\n");
        const std::string content = "q " + w + " 0 0 " + h + " 0 0 cm /Im0 Do Q\n";
        begin (pageId (i) + 1);
        put ("<< /Length " + std::to_string (content.size()) + " >>\nstream\n" + content + "endstream\nendobj\n");
        const auto packed = deflate (p.gray);
        begin (pageId (i) + 2);
        put ("<< /Type /XObject /Subtype /Image /Width " + std::to_string (p.pixelWidth) + " /Height " + std::to_string (p.pixelHeight)
             + " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /Length " + std::to_string (packed.size()) + " >>\nstream\n");
        out.insert (out.end(), packed.begin(), packed.end());
        put ("\nendstream\nendobj\n");
    }
    const size_t xref = out.size();
    put ("xref\n0 " + std::to_string (offsets.size() + 1) + "\n0000000000 65535 f \n");
    for (const auto o : offsets)
    {
        char line[24];
        std::snprintf (line, sizeof (line), "%010zu 00000 n \n", o);
        put (line);
    }
    put ("trailer\n<< /Size " + std::to_string (offsets.size() + 1) + " /Root 1 0 R /Info 3 0 R >>\nstartxref\n" + std::to_string (xref) + "\n%%EOF\n");
    return out;
}

bool writeImagePdf (const std::vector<PdfPage>& pages, const PdfInfo& info, const std::string& path, std::string* error)
{
    try
    {
        const auto bytes = imagePdf (pages, info);
        juce::File file (juce::String::fromUTF8 (path.c_str()));
        if (! file.replaceWithData (bytes.data(), bytes.size()))
            throw std::runtime_error ("파일을 쓸 수 없습니다: " + path);
        return true;
    }
    catch (const std::exception& e)
    {
        if (error != nullptr)
            *error = e.what();
        return false;
    }
}

} // namespace mr
