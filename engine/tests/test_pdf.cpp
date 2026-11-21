#include "TestFramework.h"

#include "io/Pdf.h"

#include <juce_core/juce_core.h>

#include <cstring>

using namespace mr;

namespace
{

std::string text (const std::vector<uint8_t>& b) { return std::string (b.begin(), b.end()); }

size_t count (const std::string& s, const std::string& what)
{
    size_t n = 0;
    for (auto p = s.find (what); p != std::string::npos; p = s.find (what, p + 1))
        ++n;
    return n;
}

} // namespace

TEST (pdf_pages_xref_and_lossless_images)
{
    PdfPage a;
    a.pixelWidth = 3;
    a.pixelHeight = 2;
    a.gray = { 0, 40, 80, 120, 200, 255 };
    PdfPage b;
    b.pixelWidth = b.pixelHeight = 1;
    b.gray = { 17 };
    b.widthPoints = 100.0;
    b.heightPoints = 50.5;
    const auto bytes = imagePdf ({ a, b }, { "악보", "Theme", "InitialDAW" });
    const auto s = text (bytes);
    CHECK (s.rfind ("%PDF-1.4\n", 0) == 0);
    CHECK (s.size() >= 6 && s.compare (s.size() - 6, 6, "%%EOF\n") == 0);
    CHECK_EQ (count (s, "/Type /Page /Parent"), (size_t) 2);
    CHECK (s.find ("/Count 2") != std::string::npos);
    CHECK (s.find ("/MediaBox [0 0 100.00 50.50]") != std::string::npos);
    CHECK (s.find ("/Title <FEFFC545BCF4>") != std::string::npos);   // 「악보」 의 UTF-16BE

    // 상호 참조 표의 자리마다 그 번호의 객체가 시작한다
    const auto sx = s.rfind ("startxref\n");
    REQUIRE (sx != std::string::npos);
    const size_t xref = (size_t) std::stoul (s.substr (sx + 10));
    REQUIRE (s.compare (xref, 5, "xref\n") == 0);
    std::istringstream table (s.substr (xref + 5));
    size_t first = 0, objects = 0;
    table >> first >> objects;
    CHECK_EQ (objects, (size_t) 10);   // 0, 카탈로그, 쪽 목록, 정보, 쪽마다 셋
    std::string line;
    std::getline (table, line);
    std::getline (table, line);   // 0 번
    for (size_t id = 1; id < objects; ++id)
    {
        std::getline (table, line);
        const size_t at = (size_t) std::stoul (line.substr (0, 10));
        const auto head = std::to_string (id) + " 0 obj\n";
        CHECK (s.compare (at, head.size(), head) == 0);
    }

    // 첫 그림을 풀면 넣은 픽셀 그대로
    const auto im = s.find ("/Subtype /Image");
    REQUIRE (im != std::string::npos);
    const auto lenAt = s.find ("/Length ", im) + 8;
    const size_t length = (size_t) std::stoul (s.substr (lenAt));
    const auto data = s.find ("stream\n", im) + 7;
    juce::MemoryInputStream packed (bytes.data() + data, length, false);
    juce::GZIPDecompressorInputStream unzip (&packed, false, juce::GZIPDecompressorInputStream::zlibFormat);
    juce::MemoryBlock plain;
    unzip.readIntoMemoryBlock (plain);
    REQUIRE (plain.getSize() == a.gray.size());
    CHECK (std::memcmp (plain.getData(), a.gray.data(), a.gray.size()) == 0);
}

TEST (pdf_rejects_wrong_image_size)
{
    PdfPage bad;
    bad.pixelWidth = 4;
    bad.pixelHeight = 4;
    bad.gray.assign (3, 0);
    std::string error;
    CHECK (! writeImagePdf ({ bad }, {}, juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bad.pdf").getFullPathName().toStdString(), &error));
    CHECK (error.find ("그림 크기") != std::string::npos);
    CHECK (! writeImagePdf ({}, {}, "/tmp/never.pdf", &error));
}
