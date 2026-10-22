/*
  ChordEngine host-boundary probe.

  Classification: AUTOMATED, HEAD-LESS host-format test. It is NOT a DAW test
  and NOT host-parity sign-off. Nothing here opens a window, drives another
  application, or takes the GUI test gate.

  It loads the REAL plug-in bundles through JUCE's head-less plug-in hosting
  (the same AudioPluginFormatManager + VST3/AU hosting path a DAW uses) and
  records what a HOST actually sees:

    * the audio-bus layout the plug-in declares to the host,
    * whether the host derives a MIDI input bus and a MIDI output bus,
    * whether the host classifies the plug-in as a MIDI effect,
    * the COMPLETE MIDI event list the host receives back after triggering,
      including note-offs, velocities and sample offsets, and
    * whether the raw trigger is suppressed in both directions.

  For the VST3 format this is a genuine format-boundary capture: JUCE's VST3
  host builds a `Vst::IEventList` from the incoming `MidiBuffer`, passes it to
  the loaded component through `IComponent::process`, and converts the
  component's own outgoing `Vst::IEventList` back into the reported
  `MidiBuffer` (see juce_VST3PluginFormatImpl.h, `processAudio` /
  `MidiEventList::toMidiBuffer`). The reported events are therefore the events
  the plug-in wrote to its VST3 event output, not an internal model.

  This is the format-boundary counterpart of
  ChordEngineProcessorIntegrationTests, which drives the processor class
  directly. Only this probe exercises the VST3/AU wrapper a host actually
  loads, which is what decides whether a DAW can route the generated chord
  MIDI downstream.

  Usage:
      ChordEngineHostBoundaryProbe [bundle-path ...]

  With no arguments it probes the bundles the macOS release package installs
  (~/Library/Audio/Plug-Ins/VST3/ChordEngine.vst3 and
  .../Components/ChordEngine.component).

  Exit codes: 0 = every present bundle passed, 1 = a check failed,
              77 = no bundle was present to test (CTest reports "skipped").
*/

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <set>
#include <vector>

namespace
{
constexpr double probeSampleRate = 44100.0;
constexpr int probeBlockSize = 512;

// Default C / Minor / Dreamy, WHOLE 0: trigger 60 generates this chord and the
// raw trigger note is suppressed - on note-on AND on note-off, exactly as the
// source-derived reference vectors state ("triggerSuppressed": true plus the
// note-off case in FLReferenceVectors.json). Same expectation the processor
// integration suite asserts; here it must survive the plug-in wrapper.
const std::set<int> expectedChord { 51, 55, 58, 62 };
constexpr int triggerNote = 60;
constexpr int triggerVelocity = 96;

// Two complete trigger passes, matching the two pattern passes the FL Studio
// session played. Each pass is note-on, one sustained block, note-off, then one
// silent block so any release events land inside the captured window.
constexpr int triggerPasses = 2;
constexpr int blocksPerPass = 4;
constexpr int noteOffBlock = 2;

struct CapturedEvent
{
    int pass = 0;
    int block = 0;
    int sampleOffset = 0;
    bool isNoteOn = false;
    int channel = 0;
    int note = 0;
    int velocity = 0;
};

int report (const char* level, const juce::String& text)
{
    std::printf ("%-5s %s\n", level, text.toRawUTF8());
    return 0;
}

void reportFailure (const juce::String& text)
{
    std::printf ("%-5s %s\n", "FAIL", text.toRawUTF8());
}

juce::String describe (const CapturedEvent& event)
{
    return "pass " + juce::String (event.pass)
             + "  block " + juce::String (event.block)
             + "  @" + juce::String (event.sampleOffset).paddedLeft ('0', 3)
             + "  " + (event.isNoteOn ? "note-on " : "note-off")
             + "  ch " + juce::String (event.channel)
             + "  pitch " + juce::String (event.note)
             + "  vel " + juce::String (event.velocity);
}

juce::String pitchList (const std::set<int>& pitches)
{
    juce::String text;
    for (auto pitch : pitches)
        text << pitch << " ";
    return text.trim();
}

// Returns true when the bundle produced at least one plugin description.
bool probeBundle (juce::AudioPluginFormatManager& formats,
                  const juce::String& bundlePath,
                  int& failures)
{
    auto* format = [&]() -> juce::AudioPluginFormat*
    {
        for (int i = 0; i < formats.getNumFormats(); ++i)
            if (auto* f = formats.getFormat (i); f->fileMightContainThisPluginType (bundlePath))
                return f;
        return nullptr;
    }();

    if (format == nullptr)
    {
        reportFailure (bundlePath + ": no installed host format claims this bundle");
        ++failures;
        return false;
    }

    juce::OwnedArray<juce::PluginDescription> descriptions;
    format->findAllTypesForFile (descriptions, bundlePath);

    if (descriptions.isEmpty())
    {
        reportFailure (bundlePath + ": the " + format->getName()
                         + " host could not read a plugin class from this bundle");
        ++failures;
        return false;
    }

    for (auto* description : descriptions)
    {
        juce::String error;
        auto instance = formats.createPluginInstance (*description, probeSampleRate,
                                                     probeBlockSize, error);

        std::printf ("\n--- %s [%s] class \"%s\"\n",
                     bundlePath.toRawUTF8(), format->getName().toRawUTF8(),
                     description->name.toRawUTF8());

        if (instance == nullptr)
        {
            reportFailure ("host could not instantiate: " + error);
            ++failures;
            continue;
        }

        auto* processor = instance.get();

        // ---- what the host sees ------------------------------------------
        const int audioInputBuses = processor->getBusCount (true);
        const int audioOutputBuses = processor->getBusCount (false);
        const int audioInputChannels = processor->getTotalNumInputChannels();
        const int audioOutputChannels = processor->getTotalNumOutputChannels();

        std::printf ("      version          : %s\n", description->version.toRawUTF8());
        std::printf ("      manufacturer     : %s\n", description->manufacturerName.toRawUTF8());
        std::printf ("      isMidiEffect     : %s\n", processor->isMidiEffect() ? "yes" : "no");
        std::printf ("      acceptsMidi      : %s\n", processor->acceptsMidi() ? "yes" : "no");
        std::printf ("      producesMidi     : %s\n", processor->producesMidi() ? "yes" : "no");
        std::printf ("      audio buses      : in %d / out %d (channels in %d / out %d)\n",
                     audioInputBuses, audioOutputBuses, audioInputChannels, audioOutputChannels);

        if (! processor->acceptsMidi())
        {
            reportFailure ("the host is not told the plug-in accepts MIDI input");
            ++failures;
        }

        if (! processor->producesMidi())
        {
            reportFailure ("the host is not told the plug-in produces MIDI output: "
                           "no host can route the generated chord downstream");
            ++failures;
        }

        // ---- MIDI in -> generated MIDI out -------------------------------
        // prepareToPlay activates the loaded component and enables its event
        // buses, which is the host-side precondition for both event lists.
        processor->prepareToPlay (probeSampleRate, probeBlockSize);

        std::vector<CapturedEvent> captured;

        for (int pass = 0; pass < triggerPasses; ++pass)
        {
            for (int block = 0; block < blocksPerPass; ++block)
            {
                juce::MidiBuffer events;

                if (block == 0)
                    events.addEvent (juce::MidiMessage::noteOn (1, triggerNote,
                                                               (juce::uint8) triggerVelocity), 0);
                else if (block == noteOffBlock)
                    events.addEvent (juce::MidiMessage::noteOff (1, triggerNote), 0);

                juce::AudioBuffer<float> audio (processor->getTotalNumOutputChannels(),
                                                probeBlockSize);
                audio.clear();

                processor->processBlock (audio, events);

                for (const auto metadata : events)
                {
                    const auto message = metadata.getMessage();

                    if (! (message.isNoteOn() || message.isNoteOff()))
                        continue;

                    captured.push_back (CapturedEvent { pass, block, metadata.samplePosition,
                                                        message.isNoteOn(),
                                                        message.getChannel(),
                                                        message.getNoteNumber(),
                                                        message.getVelocity() });
                }
            }
        }

        std::printf ("      outgoing events  : %d\n", (int) captured.size());

        for (const auto& event : captured)
            std::printf ("        %s\n", describe (event).toRawUTF8());

        if (captured.empty())
        {
            std::printf ("        (the loaded plug-in wrote no note events to its "
                         "VST3 event output)\n");
        }

        // ---- evaluate ----------------------------------------------------
        std::set<int> generatedOn;
        std::set<int> generatedOff;
        std::set<int> onPerPass [triggerPasses];
        std::set<int> offPerPass [triggerPasses];
        bool rawTriggerOnPassedThrough = false;
        bool rawTriggerOffPassedThrough = false;

        for (const auto& event : captured)
        {
            if (event.note == triggerNote)
            {
                if (event.isNoteOn)
                    rawTriggerOnPassedThrough = true;
                else
                    rawTriggerOffPassedThrough = true;

                continue;
            }

            if (event.isNoteOn)
            {
                generatedOn.insert (event.note);
                onPerPass[event.pass].insert (event.note);
            }
            else
            {
                generatedOff.insert (event.note);
                offPerPass[event.pass].insert (event.note);
            }
        }

        if (captured.empty())
        {
            reportFailure ("the host received no MIDI output for trigger note 60");
            ++failures;
        }

        if (rawTriggerOnPassedThrough)
        {
            reportFailure ("the raw trigger note-on passed through to the host");
            ++failures;
        }

        if (rawTriggerOffPassedThrough)
        {
            reportFailure ("the raw trigger note-off passed through to the host");
            ++failures;
        }

        if (generatedOn != expectedChord)
        {
            reportFailure ("generated chord was \"" + pitchList (generatedOn)
                             + "\", expected \"" + pitchList (expectedChord) + "\"");
            ++failures;
        }
        else
        {
            report ("PASS", "trigger 60 -> generated chord " + pitchList (expectedChord)
                              + "across the " + format->getName() + " host boundary");
        }

        if (generatedOff != expectedChord)
        {
            reportFailure ("generated note-offs were \"" + pitchList (generatedOff)
                             + "\", expected \"" + pitchList (expectedChord)
                             + "\" (a host that never sees the releases would leave "
                               "the chord sounding)");
            ++failures;
        }
        else
        {
            report ("PASS", "trigger note-off released every generated pitch");
        }

        for (int pass = 0; pass < triggerPasses; ++pass)
        {
            if (onPerPass[pass] == expectedChord && offPerPass[pass] == expectedChord)
            {
                report ("PASS", "pass " + juce::String (pass)
                                  + " generated and released the full chord");
            }
            else
            {
                reportFailure ("pass " + juce::String (pass) + " generated \""
                                 + pitchList (onPerPass[pass]) + "\" and released \""
                                 + pitchList (offPerPass[pass]) + "\"");
                ++failures;
            }
        }

        processor->releaseResources();
    }

    return true;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    std::vector<juce::String> bundles;

    for (int i = 1; i < argc; ++i)
        bundles.emplace_back (argv[i]);

    if (bundles.empty())
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        bundles.push_back (home.getChildFile ("Library/Audio/Plug-Ins/VST3/ChordEngine.vst3")
                               .getFullPathName());
        bundles.push_back (home.getChildFile ("Library/Audio/Plug-Ins/Components/ChordEngine.component")
                               .getFullPathName());
    }

    juce::AudioPluginFormatManager formats;
    juce::addHeadlessDefaultFormatsToManager (formats);

    int failures = 0;
    int presentBundles = 0;

    for (const auto& bundlePath : bundles)
    {
        if (! juce::File (bundlePath).exists())
        {
            report ("SKIP", bundlePath + " is not present on this machine");
            continue;
        }

        ++presentBundles;

        const int failuresBefore = failures;

        if (probeBundle (formats, bundlePath, failures))
            report (failures == failuresBefore ? "DONE" : "BAD", bundlePath);
    }

    if (presentBundles == 0)
    {
        std::printf ("\nChordEngineHostBoundaryProbe: no bundle available to probe "
                     "(install the release package or pass a bundle path)\n");
        return 77;
    }

    if (failures != 0)
    {
        std::printf ("\nChordEngineHostBoundaryProbe: %d check(s) failed\n", failures);
        return 1;
    }

    std::printf ("\nChordEngineHostBoundaryProbe: all host-boundary checks passed\n");
    return 0;
}
