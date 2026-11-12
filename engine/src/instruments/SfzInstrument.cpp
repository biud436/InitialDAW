#include "instruments/SfzInstrument.h"

#include "instruments/Registry.h"

#include <algorithm>
#include <cmath>
#include <vector>

#if MUSIC_RENDER_HAS_SFIZZ
 #include <sfizz.hpp>
#endif

namespace mr
{

namespace
{

// 파일 슬롯: 0 = 기본 파일(def.file), 1.. = 파일이 있는 아티큘레이션 (이름 순)
std::vector<std::pair<std::string, juce::File>> fileSlots (const InstrumentDef& def)
{
    std::vector<std::pair<std::string, juce::File>> slots;
    slots.push_back ({ std::string {}, def.file });
    for (const auto& [name, art] : def.profile.articulations)
        if (art.file != juce::File())
            slots.push_back ({ name, art.file });
    return slots;
}

// #include "경로" 를 그 파일의 내용으로 바꾼다. 경로는 최상위 SFZ 파일의 폴더 기준이다 (sfizz 와 같다).
juce::String inlineIncludes (const juce::String& text, const juce::File& rootDir, int depth = 0)
{
    if (depth > 8)
        return text;
    juce::StringArray lines;
    lines.addLines (text);
    juce::String out;
    for (const auto& line : lines)
    {
        const auto trimmed = line.trimStart();
        if (trimmed.startsWith ("#include"))
        {
            const auto path = trimmed.fromFirstOccurrenceOf ("\"", false, false).upToFirstOccurrenceOf ("\"", false, false);
            const auto included = rootDir.getChildFile (path);
            if (path.isNotEmpty() && included.existsAsFile())
            {
                out << inlineIncludes (included.loadFileAsString(), rootDir, depth + 1) << "\n";
                continue;
            }
        }
        out << line << "\n";
    }
    return out;
}

} // namespace

int sfzChannelFor (const InstrumentDef& def, const std::string& articulation)
{
    const auto slots = fileSlots (def);
    for (size_t i = 1; i < slots.size(); ++i)
        if (slots[i].first == articulation)
            return (int) i + 1;
    return 0;
}

#if MUSIC_RENDER_HAS_SFIZZ

bool sfzAvailable() { return true; }

namespace
{

class SfzInstrument final : public Instrument
{
public:
    struct Slot
    {
        std::string articulation;
        juce::File file;
        std::vector<std::unique_ptr<sfz::Sfizz>> layers;   // 레이어가 없으면 하나
    };

    SfzInstrument (std::vector<Slot> slots, std::vector<int> layerVelocities, int dynamicsController, std::string name)
        : slots_ (std::move (slots)), layerVelocities_ (std::move (layerVelocities)), dynamicsController_ (dynamicsController), name_ (std::move (name))
    {
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        sampleRate_ = sampleRate;
        smoothing_ = 1.0 - std::exp (-1.0 / (0.03 * sampleRate));   // 크로스페이드는 30ms 로 고른다
        scratch_.setSize (2, maxBlockSize);
        for (auto& slot : slots_)
            for (auto& synth : slot.layers)
            {
                synth->setSampleRate ((float) sampleRate);
                synth->setSamplesPerBlock (maxBlockSize);
            }
    }

    std::string description() const override { return "sfz:" + name_; }

    void render (juce::AudioBuffer<float>& buffer, int numSamples, const juce::MidiBuffer& midi, int64_t) override
    {
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            const int delay = std::clamp (meta.samplePosition, 0, numSamples - 1);
            if (m.isNoteOn() || m.isNoteOff())
            {
                auto& slot = slotFor (m.getChannel());
                for (size_t i = 0; i < slot.layers.size(); ++i)
                {
                    const int velocity = crossfading() ? layerVelocities_[i] : (int) m.getVelocity();
                    if (m.isNoteOn())
                        slot.layers[i]->noteOn (delay, m.getNoteNumber(), std::max (1, velocity));
                    else
                        slot.layers[i]->noteOff (delay, m.getNoteNumber(), 0);
                    if (! crossfading())
                        break;   // 크로스페이드가 아니면 첫 레이어만 쓴다
                }
            }
            else if (m.isController())
            {
                if (m.getControllerNumber() == dynamicsController_)
                {
                    dynamicsTarget_ = m.getControllerValue() / 127.0;
                    if (! dynamicsSeen_)
                        dynamics_ = dynamicsTarget_;
                    dynamicsSeen_ = true;
                }
                forEachSynth ([&] (sfz::Sfizz& s) { s.cc (delay, m.getControllerNumber(), m.getControllerValue()); });
            }
            else if (m.isPitchWheel())
                forEachSynth ([&] (sfz::Sfizz& s) { s.pitchWheel (delay, m.getPitchWheelValue() - 8192); });
            else if (m.isChannelPressure())
                forEachSynth ([&] (sfz::Sfizz& s) { s.channelAftertouch (delay, m.getChannelPressureValue()); });
            else if (m.isProgramChange())
                forEachSynth ([&] (sfz::Sfizz& s) { s.programChange (delay, m.getProgramChangeNumber()); });
        }

        buffer.clear (0, 0, numSamples);
        buffer.clear (1, 0, numSamples);
        // 레이어 게인은 32 샘플마다 다시 계산하고 그 사이는 선형으로 잇는다
        const bool fade = crossfading() && layerVelocities_.size() > 1;
        const double startValue = dynamics_;
        for (int i = 0; i < numSamples; ++i)
            dynamics_ += (dynamicsTarget_ - dynamics_) * smoothing_;
        const double endValue = dynamics_;

        for (auto& slot : slots_)
            for (size_t layer = 0; layer < slot.layers.size(); ++layer)
            {
                float* channels[2] = { scratch_.getWritePointer (0), scratch_.getWritePointer (1) };
                slot.layers[layer]->renderBlock (channels, (size_t) numSamples, 1);
                if (! fade)
                {
                    buffer.addFrom (0, 0, scratch_, 0, 0, numSamples);
                    buffer.addFrom (1, 0, scratch_, 1, 0, numSamples);
                    continue;
                }
                const float g0 = (float) layerGain (layer, startValue), g1 = (float) layerGain (layer, endValue);
                buffer.addFromWithRamp (0, 0, scratch_.getReadPointer (0), numSamples, g0, g1);
                buffer.addFromWithRamp (1, 0, scratch_.getReadPointer (1), numSamples, g0, g1);
            }
    }

    bool crossfadeEnabled = false;   // 트랙에 다이내믹스 곡선이 있을 때 켠다

private:
    bool crossfading() const { return crossfadeEnabled && layerVelocities_.size() > 1; }

    Slot& slotFor (int channel)
    {
        const size_t index = channel >= 2 ? (size_t) channel - 1 : 0;
        return index < slots_.size() && ! slots_[index].layers.empty() ? slots_[index] : slots_.front();
    }

    template <typename F>
    void forEachSynth (F&& f)
    {
        for (auto& slot : slots_)
            for (auto& synth : slot.layers)
                f (*synth);
    }

    // 레이어 i 의 게인. 값 x(0..1) 를 레이어 사이 구간에 놓고 이웃한 두 레이어를 등전력으로 섞는다.
    double layerGain (size_t i, double x) const
    {
        const size_t n = layerVelocities_.size();
        const double pos = std::clamp (x, 0.0, 1.0) * (double) (n - 1);
        const double d = std::abs (pos - (double) i);
        if (d >= 1.0)
            return 0.0;
        return std::cos (d * 1.5707963267948966);
    }

    std::vector<Slot> slots_;
    std::vector<int> layerVelocities_;
    int dynamicsController_ = 1;
    std::string name_;
    double sampleRate_ = 48000.0;
    double smoothing_ = 0.001;
    double dynamics_ = 1.0, dynamicsTarget_ = 1.0;
    bool dynamicsSeen_ = false;
    juce::AudioBuffer<float> scratch_;
};

} // namespace

std::unique_ptr<Instrument> makeSfzInstrument (const InstrumentDef& def, const InstrumentContext& ctx, std::string& error)
{
    const auto files = fileSlots (def);
    const bool automatedDynamics = std::find (ctx.automated.begin(), ctx.automated.end(), def.profile.dynamics.controller) != ctx.automated.end();
    const size_t layerCount = def.profile.dynamics.layers.size() > 1 ? def.profile.dynamics.layers.size() : 1;

    std::vector<SfzInstrument::Slot> slots;
    for (const auto& [articulation, file] : files)
    {
        SfzInstrument::Slot slot;
        slot.articulation = articulation;
        slot.file = file;
        if (file == juce::File())
        {
            slots.push_back (std::move (slot));   // 기본 파일이 없으면 빈 슬롯 (모든 아티큘레이션에 파일이 있다)
            continue;
        }
        if (! file.existsAsFile())
        {
            error = "SFZ file not found: " + file.getFullPathName().toStdString();
            return nullptr;
        }
        // 크로스페이드가 필요 없으면 레이어 하나만 불러 메모리를 아낀다
        const size_t count = automatedDynamics ? layerCount : 1;
        for (size_t i = 0; i < count; ++i)
        {
            auto synth = std::make_unique<sfz::Sfizz>();
            synth->setSampleRate ((float) ctx.sampleRate);
            synth->setSamplesPerBlock (ctx.blockSize);
            synth->setNumVoices (128);
            if (! ctx.realtime)
                synth->enableFreeWheeling();   // 오프라인: 샘플을 다 읽을 때까지 기다린다
            bool loaded = false;
            if (def.sfzRegionOpcodes.empty())
                loaded = synth->loadSfzFile (file.getFullPathName().toStdString());
            else
            {
                // 모든 <region> 뒤에 옵코드를 덧붙인다 (#include 로 들어오는 리전도). 리전의 옵코드는 global 과 group 의 값을 덮는다.
                // 원래 경로로 불러 default_path 와 샘플의 상대 경로가 그대로 풀린다.
                auto text = inlineIncludes (file.loadFileAsString(), file.getParentDirectory());
                text = text.replace ("<region>", "<region> " + juce::String (def.sfzRegionOpcodes) + "\n");
                loaded = synth->loadSfzString (file.getFullPathName().toStdString(), text.toStdString());
            }
            if (! loaded)
            {
                error = "sfizz cannot load " + file.getFullPathName().toStdString();
                return nullptr;
            }
            if (synth->getNumRegions() == 0)
            {
                error = "SFZ file has no playable regions: " + file.getFullPathName().toStdString();
                return nullptr;
            }
            slot.layers.push_back (std::move (synth));
        }
        slots.push_back (std::move (slot));
    }
    auto instrument = std::make_unique<SfzInstrument> (std::move (slots), def.profile.dynamics.layers, def.profile.dynamics.controller, def.name);
    instrument->crossfadeEnabled = automatedDynamics;
    return instrument;
}

#else

bool sfzAvailable() { return false; }

std::unique_ptr<Instrument> makeSfzInstrument (const InstrumentDef&, const InstrumentContext&, std::string& error)
{
    error = "this build has no SFZ support (configure with MUSIC_RENDER_WITH_SFIZZ=ON)";
    return nullptr;
}

#endif

} // namespace mr
