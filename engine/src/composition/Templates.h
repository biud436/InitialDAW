// 곡 템플릿: 편성, 믹서, 효과, 버스, 마스터, 템포와 박자만 갖춘 빈 곡 (templates/<id>.json, tools/make-templates.py 가 쓴다).
// 새 곡은 템플릿을 복사해 제목을 붙이고 metadata.template 에 템플릿 id 만 남긴다.
#pragma once

#include "core/Diagnostics.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace mr
{

struct SongTemplate
{
    std::string id;            // 파일 이름 (확장자 없이)
    std::string name;          // metadata.template.name
    std::string description;
    std::string source;        // 바탕이 된 곡의 폴더 이름
    std::string sourceTitle;   // 그 곡의 제목
    int order = 0;
    juce::File file;
    double bpm = 120.0;
    int beatsPerBar = 4, beatUnit = 4;
    std::optional<double> targetLufs;
    bool loop = false;
    double loopStartBeat = 0.0, loopEndBeat = 0.0;
    std::vector<std::string> trackNames;
    std::vector<std::string> instruments;
};

// MUSIC_RENDER_TEMPLATES 환경 변수, 없으면 저장소의 templates/
juce::File defaultTemplateDirectory();

// 폴더의 템플릿을 order, id 순으로. 읽지 못하는 파일은 경고로 알리고 건너뛴다.
std::vector<SongTemplate> listTemplates (const juce::File& directory, Diagnostics& diagnostics);

// 새 곡의 JSON (앱의 저장 형식). 템플릿의 설명은 지우고 metadata.template 에 id 만 남긴다.
std::string songFromTemplate (const SongTemplate& songTemplate, const std::string& title);

// 새 곡을 output 에 쓴다. 이미 있으면 Failure("io") (곡 파일을 덮어쓰지 않는다).
void createSongFromTemplate (const SongTemplate& songTemplate, const std::string& title, const juce::File& output);

} // namespace mr
