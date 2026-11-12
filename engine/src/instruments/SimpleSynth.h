// 외부 파일이 필요 없는 내장 신스. 테스트와 스케치용이며 결정적이다 (잡음도 씨앗에서 나온다).
// 프리셋의 값(필터, 엔벨로프, 비브라토, 출력)은 매개변수 표로 내놓고, 레지스트리와 트랙이 덮을 수 있다.
#pragma once

#include "core/Parameter.h"
#include "instruments/Instrument.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mr
{

class Diagnostics;
class TempoMap;
namespace json
{
class ObjectReader;
}

using SynthValues = std::map<std::string, double>;

std::vector<std::string> simpleSynthPresets();
// 프리셋의 합성 방식: subtractive, piano, pluck, drums (모르는 프리셋이면 빈 문자열)
std::string synthEngineName (const std::string& preset);
// 프리셋의 매개변수 표 (화면 순서). defaultValue 는 프리셋의 값, base 가 있으면 그 값이 기본값이다 (레지스트리의 parameters).
std::vector<ParameterInfo> synthParameters (const std::string& preset, const SynthValues* base = nullptr);
// 모든 프리셋을 통틀어 그 키의 범위 (곡을 읽을 때 검사한다). 없으면 nullptr.
const ParameterInfo* findSynthParameter (const std::string& key);
std::vector<std::string> synthParameterKeys();
// 고르는 매개변수(choiceNames 가 있는 것)의 이름을 값으로 (모르는 이름이면 nullopt)
std::optional<double> synthChoiceValue (const ParameterInfo& p, const std::string& name);
// 곡과 레지스트리의 신스 값 하나: 범위 안의 수, 또는 고르는 매개변수의 이름 ("filterMode": "highpass"). 잘못되면 오류를 쓰고 nullopt.
std::optional<double> readSynthValue (json::ObjectReader& r, const std::string& key, const ParameterInfo& p, Diagnostics& d);

// 화면의 그래프: 감산 프리셋의 오실레이터를 섞은 파형의 한 주기 (가장 낮은 옥타브의 주기, 최댓값 1 로 맞춘다, 감산이 아니면 빈 목록)
std::vector<float> synthWaveform (const std::string& preset, const SynthValues& values, int points);
// 신스 필터(2극 SVF)의 주파수 응답 (dB). 처리와 같은 수식이다. mode 0 저역 통과, 1 고역 통과, 2 대역 통과 (가운데에서 0 dB).
std::vector<double> synthFilterResponseDb (double cutoffHz, double resonance, double sampleRate, const std::vector<double>& frequenciesHz,
                                           int mode = 0);

// base 다음에 track 의 값을 덮는다. tempo 는 박 단위 길이의 효과음(club-drums)이 쓴다 (사본을 둔다, 없으면 120 BPM).
// 실패하면 nullptr 와 error.
std::unique_ptr<Instrument> makeSimpleSynth (const std::string& preset, uint64_t seed, std::string& error,
                                             const SynthValues* base = nullptr, const SynthValues* track = nullptr,
                                             const TempoMap* tempo = nullptr);

} // namespace mr
