#include "composition/Groove.h"

#include "core/Json.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mr
{

namespace
{
long long nearestIndex (const Groove& g, double beat) { return (long long) std::llround (beat / g.grid); }

const GrooveStep& stepAt (const Groove& g, long long index)
{
    const auto n = (long long) g.steps.size();
    return g.steps[(size_t) (((index % n) + n) % n)];
}

// 짝의 둘째 음이 짝 길이의 percent 에 온다 (50 은 곧게)
Groove swing (const std::string& id, const std::string& name, double grid, double percent)
{
    Groove g;
    g.id = id;
    g.name = name;
    g.grid = grid;
    g.steps = { { 0.0, 1.0 }, { (percent / 100.0 - 0.5) * 2.0 * grid, 1.0 } };
    return g;
}

double median (std::vector<double> v)
{
    std::sort (v.begin(), v.end());
    return v.size() % 2 == 1 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
}

double tidy (double v) { return std::round (v * 1.0e5) / 1.0e5; }

// 칸마다 모은 어긋남과 세기로
Groove collect (const std::vector<double>& beats, const std::vector<double>& weights, double grid, double lengthBeats)
{
    Groove g;
    g.grid = grid;
    const int count = std::max (1, (int) std::lround (lengthBeats / grid));
    std::vector<std::vector<double>> offsets ((size_t) count), strengths ((size_t) count);
    for (size_t i = 0; i < beats.size(); ++i)
    {
        const auto index = (long long) std::llround (beats[i] / grid);
        const auto k = (size_t) (((index % count) + count) % count);
        offsets[k].push_back (beats[i] - (double) index * grid);
        strengths[k].push_back (weights[i]);
    }
    const double all = weights.empty() ? 1.0 : std::accumulate (weights.begin(), weights.end(), 0.0) / (double) weights.size();
    for (int k = 0; k < count; ++k)
    {
        GrooveStep s;
        if (! offsets[(size_t) k].empty())
        {
            s.offset = tidy (median (offsets[(size_t) k]));
            const double mean = std::accumulate (strengths[(size_t) k].begin(), strengths[(size_t) k].end(), 0.0) / (double) strengths[(size_t) k].size();
            s.velocity = all > 0.0 ? tidy (std::clamp (mean / all, 0.25, 2.0)) : 1.0;
        }
        g.steps.push_back (s);
    }
    return g;
}
} // namespace

const std::vector<Groove>& factoryGrooves()
{
    static const std::vector<Groove> grooves = []
    {
        std::vector<Groove> v;
        for (const int p : { 54, 58, 62, 66 })
            v.push_back (swing ("swing-16-" + std::to_string (p), "16th swing " + std::to_string (p) + "%", 0.25, p));
        for (const int p : { 55, 60, 67 })
            v.push_back (swing ("swing-8-" + std::to_string (p), "8th swing " + std::to_string (p) + "%", 0.5, p));
        Groove laid;
        laid.id = "laid-back";
        laid.name = "Laid back";
        laid.grid = 0.25;
        laid.steps = { { 0.0, 1.1 }, { 0.02, 0.8 }, { 0.012, 0.95 }, { 0.03, 0.8 } };
        v.push_back (laid);
        Groove push;
        push.id = "push";
        push.name = "Push";
        push.grid = 0.25;
        push.steps = { { 0.0, 1.1 }, { -0.015, 0.85 }, { -0.01, 1.0 }, { -0.02, 0.85 } };
        v.push_back (push);
        Groove backbeat;
        backbeat.id = "backbeat-accent";
        backbeat.name = "Backbeat accent";
        backbeat.grid = 1.0;
        backbeat.steps = { { 0.0, 0.9 }, { 0.0, 1.2 }, { 0.0, 0.9 }, { 0.0, 1.2 } };
        v.push_back (backbeat);
        return v;
    }();
    return grooves;
}

const Groove* findFactoryGroove (const std::string& id)
{
    for (const auto& g : factoryGrooves())
        if (g.id == id)
            return &g;
    return nullptr;
}

std::optional<Groove> grooveFromJson (const juce::var& json, std::string& error)
{
    if (json.getDynamicObject() == nullptr)
    {
        error = "a groove is an object";
        return std::nullopt;
    }
    Groove g;
    g.name = json.getProperty ("name", "").toString().toStdString();
    g.id = json.getProperty ("id", "").toString().toStdString();
    const auto grid = json.getProperty ("grid", juce::var());
    if (! json::isNumber (grid) || (double) grid <= 0.0 || (double) grid > 4.0)
    {
        error = "grid must be a number of beats above 0 and at most 4";
        return std::nullopt;
    }
    g.grid = (double) grid;
    const auto* steps = json.getProperty ("steps", juce::var()).getArray();
    if (steps == nullptr || steps->isEmpty() || steps->size() > 256)
    {
        error = "steps must be a list of 1 to 256 steps";
        return std::nullopt;
    }
    for (const auto& s : *steps)
    {
        GrooveStep step;
        step.offset = (double) s.getProperty ("offset", 0.0);
        step.velocity = (double) s.getProperty ("velocity", 1.0);
        if (std::abs (step.offset) >= g.grid || step.velocity <= 0.0 || step.velocity > 4.0)
        {
            error = "each step's offset must stay inside one grid step and its velocity between 0 and 4";
            return std::nullopt;
        }
        g.steps.push_back (step);
    }
    return g;
}

juce::var grooveJson (const Groove& g)
{
    auto* o = new juce::DynamicObject();
    if (! g.id.empty())
        o->setProperty ("id", juce::String (g.id));
    o->setProperty ("name", juce::String::fromUTF8 (g.name.c_str()));
    o->setProperty ("grid", g.grid);
    juce::Array<juce::var> steps;
    for (const auto& s : g.steps)
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("offset", s.offset);
        p->setProperty ("velocity", s.velocity);
        steps.add (juce::var (p));
    }
    o->setProperty ("steps", steps);
    return juce::var (o);
}

double grooveTarget (const Groove& g, double beat)
{
    if (g.steps.empty() || g.grid <= 0.0)
        return beat;
    const auto index = nearestIndex (g, beat);
    return (double) index * g.grid + stepAt (g, index).offset;
}

double grooveVelocity (const Groove& g, double beat)
{
    if (g.steps.empty() || g.grid <= 0.0)
        return 1.0;
    return stepAt (g, nearestIndex (g, beat)).velocity;
}

Groove grooveFromNotes (const std::vector<Note>& notes, double from, double to, double grid, double lengthBeats)
{
    std::vector<double> beats, weights;
    for (const auto& n : notes)
        if (n.startBeat >= from && n.startBeat < to)
        {
            beats.push_back (n.startBeat);
            weights.push_back ((double) n.velocity);
        }
    return collect (beats, weights, grid, lengthBeats);
}

Groove grooveFromHits (const std::vector<double>& beats, const std::vector<double>& levelsDb, double grid, double lengthBeats)
{
    std::vector<double> weights;
    const double mean = levelsDb.empty() ? 0.0 : std::accumulate (levelsDb.begin(), levelsDb.end(), 0.0) / (double) levelsDb.size();
    for (size_t i = 0; i < beats.size(); ++i)
        weights.push_back (std::pow (10.0, ((i < levelsDb.size() ? levelsDb[i] : mean) - mean) / 40.0));
    return collect (beats, weights, grid, lengthBeats);
}

NoteEditResult applyGroove (const std::vector<EditNote>& notes, const NoteSelection& selection, const Groove& groove, double timing, double velocity)
{
    NoteEditResult out { notes, selection };
    if (groove.steps.empty() || groove.grid <= 0.0)
        return out;
    timing = std::clamp (timing, 0.0, 1.0);
    velocity = std::clamp (velocity, 0.0, 1.0);
    auto visit = [&] (size_t i)
    {
        auto& n = out.notes[i].note;
        const double target = grooveTarget (groove, n.startBeat);
        const double factor = 1.0 + (grooveVelocity (groove, n.startBeat) - 1.0) * velocity;
        n.startBeat = std::max (0.0, std::round ((n.startBeat + (target - n.startBeat) * timing) * 1.0e6) / 1.0e6);
        n.velocity = std::clamp ((int) std::lround (n.velocity * factor), 1, 127);
    };
    if (selection.empty())
        for (size_t i = 0; i < notes.size(); ++i)
            visit (i);
    else
        for (const size_t i : selection)
            if (i < notes.size())
                visit (i);
    return out;
}

} // namespace mr
