// 쪽마다 회색조 그림 하나를 담는 PDF (악보의 PDF 내보내기와 인쇄). 그림은 무손실 Flate 압축이고 쪽 크기는 포인트 단위다.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mr
{

struct PdfPage
{
    int pixelWidth = 0, pixelHeight = 0;
    std::vector<uint8_t> gray;                       // 위에서 아래로, 한 픽셀 8비트 (0 검정, 255 흰색)
    double widthPoints = 595.28, heightPoints = 841.89;   // A4
};

struct PdfInfo
{
    std::string title, subject;                      // UTF-8
    std::string creator = "InitialDAW";
};

std::vector<uint8_t> imagePdf (const std::vector<PdfPage>& pages, const PdfInfo& info = {});
bool writeImagePdf (const std::vector<PdfPage>& pages, const PdfInfo& info, const std::string& path, std::string* error = nullptr);

} // namespace mr
