// 트랙 하나를 그 트랙 사슬의 소리로 오디오 파일에 굳힌다 (바운스). 만들기(플러그인 악기는 메시지 스레드)와 돌리기를 나눈다.
#pragma once

#include "composition/Composition.h"
#include "rendering/OfflineRenderer.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>

namespace mr
{

struct BounceOptions
{
    double sampleRate = 48000.0;
    int bitDepth = 24;
    bool postFader = false;               // 참이면 페이더와 밸런스(곡선 포함)까지, 아니면 악기와 트랙 효과까지
    const std::atomic<bool>* cancel = nullptr;
    std::atomic<float>* progress = nullptr;
    juce::File pluginCache;
    bool useDefaultRegistry = true;
};

struct BounceResult
{
    bool silent = false;       // 소리가 없다 (파일을 쓰지 않았다)
    juce::File file;
    double startBeat = 0.0;    // 소리가 시작되는 마디의 첫 박
    double lengthSeconds = 0.0;
    double peakDb = -1.0e9;
    std::vector<Diagnostic> warnings;
};

// 바운스에 쓰는 곡: 그 트랙과 그 트랙의 사이드체인 키 트랙만 울리고(솔로는 지운다), 버스와 마스터의 효과와 목표 LUFS 는 뺀다
Composition bounceComposition (const Composition& composition, size_t track);

class BounceRender
{
public:
    BounceRender (const Composition& composition, size_t track, const BounceOptions& options);   // 악기를 만든다
    ~BounceRender();
    BounceRender (const BounceRender&) = delete;
    BounceRender& operator= (const BounceRender&) = delete;

    // output 에 24비트 WAV 를 쓴다 (있으면 실패). 소리의 앞은 그 마디의 첫 박에서, 끝은 마지막 소리 뒤 0.1초에서 자른다
    BounceResult run (const juce::File& output);

private:
    Composition composition_;
    std::string trackName_;
    BounceOptions options_;
    juce::File stemsDir_;
    std::unique_ptr<OfflineRender> render_;
};

} // namespace mr
