// 영상 기능 가운데 플랫폼과 상관없는 계산: 확장자, 장면 후보 고르기, 시험 영상의 프레임 번호 읽기
#include "media/Video.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace mr::media
{

namespace
{

std::string lowerExtension (const std::string& path)
{
    auto ext = std::filesystem::path (path).extension().string();
    std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return ext;
}

constexpr int patternBits = TestPattern::numberBits;

} // namespace

bool isVideoFileName (const std::string& path)
{
    const auto ext = lowerExtension (path);
    return ext == ".mp4" || ext == ".mov" || ext == ".m4v";
}


std::vector<size_t> pickSceneChanges (const std::vector<double>& d, double fps, const SceneOptions& options)
{
    std::vector<size_t> picked;
    if (d.size() < 2 || ! (fps > 0.0))
        return picked;
    const int window = std::max (2, (int) std::lround (0.5 * fps));
    const int gap = std::max (1, (int) std::lround (options.minGapSeconds * fps));
    std::vector<double> around;
    for (size_t k = 1; k < d.size(); ++k)
    {
        if (d[k] < options.threshold)
            continue;
        around.clear();
        for (int j = -window; j <= window; ++j)
        {
            const long i = (long) k + j;
            if (j == 0 || i < 1 || i >= (long) d.size())
                continue;
            around.push_back (d[(size_t) i]);
        }
        double median = 0.0;
        if (! around.empty())
        {
            std::nth_element (around.begin(), around.begin() + (long) around.size() / 2, around.end());
            median = around[around.size() / 2];
        }
        if (d[k] < options.ratio * median)
            continue;
        if (! picked.empty() && (long) k - (long) picked.back() < gap)
        {
            if (d[k] > d[picked.back()])
                picked.back() = k;
            continue;
        }
        picked.push_back (k);
    }
    return picked;
}


int64_t readPatternFrame (const VideoImage& image)
{
    if (image.empty() || image.width < patternBits || image.height < 9)
        return -1;
    const double blockW = (double) image.width / patternBits, rowH = (double) image.height / 9.0;
    const int cy = (int) (rowH * 0.5);
    const int ry = std::max (0, (int) (rowH * 0.25)), rx = std::max (0, (int) (blockW * 0.25));
    uint32_t gray = 0;
    for (int bit = 0; bit < patternBits; ++bit)
    {
        const int cx = (int) ((bit + 0.5) * blockW);
        double sum = 0.0;
        int count = 0;
        for (int y = std::max (0, cy - ry); y <= std::min (image.height - 1, cy + ry); ++y)
            for (int x = std::max (0, cx - rx); x <= std::min (image.width - 1, cx + rx); ++x)
            {
                const uint8_t* px = &image.rgba[((size_t) y * (size_t) image.width + (size_t) x) * 4];
                sum += 0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2];
                ++count;
            }
        const double mean = count > 0 ? sum / count : 0.0;
        if (mean > 70.0 && mean < 180.0)
            return -1;
        gray = (gray << 1) | (mean >= 128.0 ? 1u : 0u);
    }
    uint32_t n = gray;
    for (uint32_t shift = 1; shift < 32; shift <<= 1)
        n ^= n >> shift;
    return (int64_t) n;
}

} // namespace mr::media
