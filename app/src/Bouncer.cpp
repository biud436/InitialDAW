#include "Bouncer.h"

#include "Exporter.h"
#include "composition/Arrange.h"
#include "composition/Document.h"
#include "core/Diagnostics.h"

#include <juce_events/juce_events.h>

#include <cmath>

namespace daw
{

juce::File nextBounceFile (const juce::File& songFile, const std::string& trackName)
{
    const auto dir = songFile.getParentDirectory().getChildFile ("audio");
    auto stem = juce::File::createLegalFileName (juce::String::fromUTF8 (trackName.c_str()));
    if (stem.isEmpty())
        stem = "track";
    for (int n = 1;; ++n)
    {
        const auto f = dir.getChildFile (stem + " bounce " + juce::String (n) + ".wav");
        if (! f.exists())
            return f;
    }
}

juce::var withBounce (const juce::var& root, const std::string& trackName, const mr::BounceResult& result, const juce::File& songFolder)
{
    const auto* tracks = root["tracks"].getArray();
    if (tracks == nullptr)
        return {};
    int index = -1;
    for (int i = 0; i < tracks->size(); ++i)
        if ((*tracks)[i]["name"].toString() == juce::String::fromUTF8 (trackName.c_str()))
            index = i;
    if (index < 0)
        return {};
    const auto original = (*tracks)[index];
    auto t = mr::newTrackJson (mr::uniqueTrackName (root, trackName + " " + juce::String::fromUTF8 ("오디오").toStdString()), "audio");
    auto* o = t.getDynamicObject();
    o->removeProperty ("notes");
    for (const auto* key : { "color", "mixer", "sends" })
        if (original.hasProperty (key))
            o->setProperty (key, original[key].clone());
    if (auto* mixer = o->getProperty ("mixer").getDynamicObject())
    {
        mixer->removeProperty ("mute");
        mixer->removeProperty ("solo");
    }
    mr::AudioClip clip;
    clip.file = result.file.getRelativePathFrom (songFolder).replaceCharacter ('\\', '/').toStdString();
    clip.startBeat = result.startBeat;
    clip.lengthSeconds = std::round (result.lengthSeconds * 1.0e6) / 1.0e6;
    t = mr::withClipAdded (t, clip);

    auto out = mr::withTrackInserted (root, (size_t) index + 1, t);
    auto* list = out["tracks"].getArray();
    auto muted = (*list)[index].clone();
    auto* track = muted.getDynamicObject();
    auto mixer = track->getProperty ("mixer");
    if (! mixer.isObject())
        mixer = juce::var (new juce::DynamicObject());
    mixer.getDynamicObject()->setProperty ("mute", true);
    mixer.getDynamicObject()->removeProperty ("solo");
    track->setProperty ("mixer", mixer);
    list->set (index, muted);
    return out;
}

BounceJob::BounceJob (mr::Composition composition, size_t track, juce::File output, std::function<void (const BounceOutcome&)> onDone)
    : juce::Thread ("InitialDAW bounce"), composition_ (std::move (composition)), track_ (track), output_ (std::move (output)), onDone_ (std::move (onDone))
{
}

BounceJob::~BounceJob()
{
    *alive_ = false;
    cancel_.store (true);
    stopThread (-1);
    render_.reset();
}

void BounceJob::start()
{
    outputExisted_ = output_.exists();
    if (usesPluginInstruments (composition_))
    {
        // 플러그인 악기는 메시지 스레드에서만 만들 수 있다
        mr::BounceOptions o;
        o.cancel = &cancel_;
        o.progress = &progress_;
        try
        {
            render_ = std::make_unique<mr::BounceRender> (composition_, track_, o);
        }
        catch (const std::exception& e)
        {
            buildError_ = juce::String::fromUTF8 (e.what());
        }
    }
    startThread();
}

void BounceJob::run()
{
    BounceOutcome outcome;
    try
    {
        if (buildError_.isNotEmpty())
            throw mr::Failure ("instrument", buildError_.toStdString());
        if (render_ == nullptr)
        {
            mr::BounceOptions o;
            o.cancel = &cancel_;
            o.progress = &progress_;
            render_ = std::make_unique<mr::BounceRender> (composition_, track_, o);
        }
        outcome.result = render_->run (output_);
        outcome.ok = true;
    }
    catch (const mr::Failure& e)
    {
        outcome.cancelled = e.code() == "cancelled";
        outcome.error = juce::String::fromUTF8 (e.what());
    }
    catch (const std::exception& e)
    {
        outcome.error = juce::String::fromUTF8 (e.what());
    }
    if (! outcome.ok && ! outputExisted_)
        output_.deleteFile();   // 쓰다 만 파일
    std::weak_ptr<bool> alive = alive_;
    juce::MessageManager::callAsync ([this, alive, outcome]
    {
        if (auto a = alive.lock(); a == nullptr || ! *a || ! onDone_)
            return;
        onDone_ (outcome);
    });
}

} // namespace daw
