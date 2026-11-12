// VST3 와 AU 호스팅 (JUCE). 검색, 메타데이터 캐시, 매개변수와 상태, 오프라인 렌더링용 악기.
#pragma once

#include "core/Diagnostics.h"
#include "instruments/Instrument.h"
#include "plugins/PluginSettings.h"
#include "timing/MeterMap.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mr
{

class TempoMap;

struct PluginHostContext
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    const TempoMap* tempo = nullptr;
    int beatsPerBar = 4;
    int beatUnit = 4;
    bool realtime = false;   // false 면 플러그인을 오프라인(non-realtime) 모드로 준비한다
    std::shared_ptr<const MeterMap> meters;   // 있으면 마디와 박자를 이 지도로 알린다
};

class PluginHost
{
public:
    static PluginHost& get();

    // format: "vst3" 또는 "au". paths 가 비면 그 형식의 기본 위치를 찾는다.
    std::vector<juce::PluginDescription> scan (const std::string& format, const std::vector<std::string>& paths,
                                               std::vector<Diagnostic>& problems);

    std::unique_ptr<juce::AudioPluginInstance> load (const std::string& format, const std::string& fileOrIdentifier,
                                                     const std::string& nameFilter, double sampleRate, int blockSize,
                                                     juce::PluginDescription& chosen, std::string& error);

    bool supports (const std::string& format) const;
    // 사용자가 직접 지정한 플러그인(명령줄 인자, 시험): 설치된 플러그인 폴더 밖이어도 불러온다. 파일이나 폴더
    void allowLocation (const std::string& path);

private:
    PluginHost();
    juce::AudioPluginFormat* formatFor (const std::string& format) const;

    bool locationAllowed (const juce::File& plugin) const;
    juce::AudioPluginFormatManager manager_;
    std::vector<std::string> allowed_;   // allowLocation 의 실제 경로
};

// 곡과 레지스트리가 가리키는 VST3 를 불러와도 되는가: 설치된 플러그인 폴더(VST3 의 기본 검색 위치, /Library/Audio/Plug-Ins,
// ~/Library/Audio/Plug-Ins), external/plugins (MUSIC_RENDER_PLUGINS), MUSIC_RENDER_PLUGIN_PATH 의 폴더(콜론으로 나눈다) 안이거나 cache 에 있는 플러그인.
// 곡 폴더에 딸려 온 플러그인이 곡을 열 때 실행되지 않게 한다. 링크와 .. 을 푼 실제 경로로 견준다
bool pluginLocationAllowed (const juce::File& plugin, const juce::File& cache);

juce::var describeEntry (const juce::PluginDescription& d);
juce::var describeInstance (juce::AudioPluginInstance& instance, const juce::PluginDescription& d);

// 상태, 프로그램, 매개변수 순으로 적용한다 (적은 매개변수가 상태를 덮는다). 문제는 problems 에 쌓는다.
// onlyChanges 면 지금 값과 같은 것은 다시 넣지 않는다 (상태는 바이트가 같으면, 매개변수는 정규화 값이 같으면).
void applyPluginSettings (juce::AudioPluginInstance& instance, const PluginSettings& settings,
                          const std::string& path, std::vector<Diagnostic>& problems, bool onlyChanges = false);
// 적용하지 않고 문제만 찾는다 (없는 매개변수, 범위 밖 프로그램, base64 가 아닌 상태)
void checkPluginSettings (juce::AudioPluginInstance& instance, const PluginSettings& settings,
                          const std::string& path, std::vector<Diagnostic>& problems);
// 플러그인의 지금 상태 (base64)
std::string pluginStateBase64 (juce::AudioPluginInstance& instance);

// settings 다음에 extra 를 적용한다 (레지스트리 항목, 그다음 트랙의 설정).
std::unique_ptr<Instrument> makePluginInstrument (const std::string& format, const std::string& fileOrIdentifier,
                                                  const std::string& nameFilter, const PluginSettings& settings,
                                                  const PluginHostContext& context, const std::string& path, std::string& error,
                                                  const PluginSettings* extra = nullptr, const std::string& extraPath = {});

// 메타데이터 캐시 (plugins scan 이 쓴다). 기본은 ~/.cache/music-render/plugins.json
juce::File defaultPluginCache();
void writePluginCache (const juce::File& file, const std::vector<juce::PluginDescription>& plugins);
std::optional<juce::PluginDescription> findCachedPlugin (const juce::File& cache, const std::string& format, const std::string& name);

// 플러그인을 쓰는 동안 살아 있어야 하는 JUCE 런타임(메시지 스레드)과 플러그인 호스트.
// main 의 지역 변수로 둔다. 정적 변수로 두면 프로세스가 끝날 때 JUCE 의 다른 정적 객체보다 늦게 정리되어 죽는다.
class HostSession
{
public:
    HostSession();
    ~HostSession();
    HostSession (const HostSession&) = delete;
    HostSession& operator= (const HostSession&) = delete;

    static bool active();
};

} // namespace mr
