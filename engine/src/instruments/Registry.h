// 악기 레지스트리. 악곡은 "instrument": "piano" 처럼 이름만 쓰고, 이름이 무엇으로 연주되는지는 여기서 정한다.
#pragma once

#include "timing/MeterMap.h"

#include "core/Diagnostics.h"
#include "instruments/Instrument.h"
#include "instruments/Profile.h"
#include "plugins/PluginHost.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mr
{

struct InstrumentDef
{
    std::string name;
    std::string type;          // synth, vst3, au, sfz (4단계)
    std::string preset;        // synth
    juce::File file;           // sfz: 기본 SFZ 파일 (아티큘레이션마다 file 을 따로 줄 수도 있다)
    std::string sfzRegionOpcodes;   // sfz: 모든 <region> 에 덧붙일 옵코드 (예: "ampeg_attack_oncc102=0.45")
    std::string plugin;        // vst3: .vst3 경로 (레지스트리 파일 기준), au: JUCE 식별자 ("AudioUnit:Synths/aumu,dls ,appl")
    std::string pluginName;    // 한 파일에 여러 플러그인이 있을 때 고르는 이름. plugin 이 비면 캐시에서 이름으로 찾는다
    PluginSettings pluginSettings;
    std::map<std::string, double> synthParameters;   // synth: 프리셋 위에 덮는 값
    InstrumentProfile profile;
    double gainDb = 0.0;
    int transpose = 0;
    std::map<int, int> keymap;   // 악기에 보내는 건반 바꾸기 (이조한 뒤의 건반 -> 악기의 건반). 드럼 세트를 GM 배치로 칠 때
    std::map<int, std::string> keyNames;   // 건반의 이름 (곡의 건반 -> "킥"). 드럼과 타악기를 피아노 롤에 보일 때
    std::string fallback;      // 만들 수 없을 때 대신 쓸 악기 이름
    std::string description;
    juce::File registryFile;   // 비면 내장
    std::string path;          // 진단용 "<파일>:$.instruments.<이름>"
};

class Registry
{
public:
    Registry();   // 내장 synth.<preset> 들을 담는다

    void addFile (const juce::File& file, Diagnostics& diagnostics);
    // 곡 안의 정의 (이름에서 정의로). 상대 경로는 곡 파일의 폴더 기준이다.
    void addInline (const juce::var& table, const juce::File& songFile, const std::string& path, Diagnostics& diagnostics);
    const InstrumentDef* find (const std::string& name) const;
    std::vector<std::string> names() const;
    const std::map<std::string, InstrumentDef>& all() const noexcept { return defs_; }
    const std::vector<juce::File>& files() const noexcept { return files_; }

private:
    void addRoot (const juce::var& root, const juce::File& file, const std::string& rootPath, Diagnostics& diagnostics);

    std::map<std::string, InstrumentDef> defs_;
    std::vector<juce::File> files_;
};

// MUSIC_RENDER_REGISTRY 환경 변수, 아니면 빌드에 박힌 engine/instruments/registry.json
juce::File defaultRegistryFile();

class TempoMap;

struct InstrumentContext
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    uint64_t seed = 0;
    const TempoMap* tempo = nullptr;   // 플러그인의 플레이헤드
    int beatsPerBar = 4;
    int beatUnit = 4;
    std::shared_ptr<const MeterMap> meters;   // 박자가 바뀌는 곡의 플레이헤드
    juce::File pluginCache;            // 비면 defaultPluginCache()
    std::vector<int> automated;        // 이 트랙에서 곡선이 있는 컨트롤러 (sfz 레이어 크로스페이드가 쓴다)
    const PluginSettings* trackPlugin = nullptr;   // 트랙의 플러그인 설정 (레지스트리 설정 뒤에 적용한다)
    const std::map<std::string, double>* trackSynth = nullptr;   // 트랙의 내장 신스 값 (레지스트리 값 뒤에)
    std::string trackPluginPath;
    bool realtime = false;             // 실시간 재생: 샘플 읽기를 기다리지 않고 플러그인을 실시간 모드로 둔다
    const std::vector<AudioClip>* trackClips = nullptr;   // 오디오 트랙의 클립
    juce::File songFolder;             // 클립 파일의 상대 경로 기준
    std::vector<std::string>* warnings = nullptr;          // 만들며 알릴 것 (읽지 못한 클립 파일)
};

// "${VAR}" 와 "${VAR:-기본값}" 을 환경 변수로 바꾸고, 상대 경로는 base 기준으로 푼다.
juce::File resolveRegistryPath (const std::string& path, const juce::File& base);

struct CreatedInstrument
{
    std::unique_ptr<Instrument> instrument;
    const InstrumentDef* def = nullptr;   // 실제로 만든 정의 (대체되었으면 대체 악기)
    std::vector<std::string> notes;       // 대체 사유 등
};

// 실패하면 Failure("instrument", ...) 를 던진다.
CreatedInstrument createInstrument (const Registry& registry, const std::string& name, const InstrumentContext& context);

} // namespace mr
