// =============================================================================
// VST3HostTest.mm - loads the built ChordEngine.vst3 the way a host does
// (VST 3 SDK hosting helpers, in-process; nothing is installed) and checks:
//
//   identity    one Audio Module class, 0.5.1's class ID, name, vendor,
//               "Instrument" category, version = this build
//   buses       stereo audio out "Output", event in "MIDI Input", event out
//               "MIDI Output"; mono accepted, 4 channels refused
//   parameters  0.5.1's IDs: bypass 'byps' + 16 x 130 MIDI controller
//               parameters at 0x6d636d00; IMidiMapping maps CC64 to one
//   0.5.1 state the exact bytes 0.5.1's VST3 wrote (with its private-data
//               trailer) restore every field; this build's state reloads
//   MIDI        a note produces the four-note chord on the event output;
//               sustain through the CC parameter holds the chord; real-time
//               safe (RtProbe) when the probe is injected
// =============================================================================
#import <Foundation/Foundation.h>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
int failures = 0;
void check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string stateOf(IComponent* component)
{
    MemoryStream stream;
    component->getState(&stream);
    return std::string(stream.getData(), static_cast<std::size_t>(stream.getSize()));
}

bool setStateBytes(IComponent* component, const std::string& bytes)
{
    MemoryStream stream(const_cast<char*>(bytes.data()), static_cast<TSize>(bytes.size()));
    return component->setState(&stream) == kResultOk;
}

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

using ArmFn = void (*)(int);
using CountFn = long (*)();
} // namespace

int main(int argc, const char* argv[])
{
    @autoreleasepool
    {
        if (argc != 3)
        {
            std::fprintf(stderr, "usage: VST3HostTest <ChordEngine.vst3> <fixture dir>\n");
            return 2;
        }
        const std::string fixtures = argv[2];
        std::string error;
        auto module = VST3::Hosting::Module::create(argv[1], error);
        check(module != nullptr, "the module loads (" + error + ")");
        if (!module)
            return 1;
        auto factory = module->getFactory();

        // ---- identity -------------------------------------------------------------
        int audioClasses = 0;
        VST3::Hosting::ClassInfo processorClass;
        for (const auto& info : factory.classInfos())
            if (info.category() == kVstAudioEffectClass)
            {
                ++audioClasses;
                processorClass = info;
            }
        check(audioClasses == 1, "exactly one Audio Module class");
        check(processorClass.ID().toString() == "ABCDEF019182FAEB4D7573634365666C",
              "class ID is 0.5.1's: " + processorClass.ID().toString());
        check(processorClass.name() == "ChordEngine" && processorClass.vendor() == "Music-Prod",
              "name ChordEngine, vendor Music-Prod");
        check(processorClass.subCategoriesString() == "Instrument", "category Instrument");
        check(processorClass.version() == CHORDENGINE_PLUGIN_VERSION_STRING,
              "version " + processorClass.version());

        HostApplication host;
        auto component = factory.createInstance<IComponent>(processorClass.ID());
        check(component && component->initialize(&host) == kResultOk, "the component initialises");
        if (!component)
            return 1;
        FUnknownPtr<IAudioProcessor> processor(component);
        FUnknownPtr<IEditController> controller(component);
        FUnknownPtr<IMidiMapping> mapping(component);
        check(processor && controller, "single component: IAudioProcessor and IEditController on one object");

        // ---- buses ------------------------------------------------------------------
        BusInfo info {};
        check(component->getBusCount(kAudio, kOutput) == 1 && component->getBusCount(kAudio, kInput) == 0,
              "one audio output, no audio input");
        component->getBusInfo(kAudio, kOutput, 0, info);
        check(info.channelCount == 2 && std::u16string(reinterpret_cast<const char16_t*>(info.name)) == u"Output",
              "audio output: stereo, named Output");
        check(component->getBusCount(kEvent, kInput) == 1 && component->getBusCount(kEvent, kOutput) == 1,
              "one MIDI input and one MIDI output bus");
        component->getBusInfo(kEvent, kInput, 0, info);
        check(std::u16string(reinterpret_cast<const char16_t*>(info.name)) == u"MIDI Input", "event input named MIDI Input");
        component->getBusInfo(kEvent, kOutput, 0, info);
        check(std::u16string(reinterpret_cast<const char16_t*>(info.name)) == u"MIDI Output", "event output named MIDI Output");
        SpeakerArrangement mono = SpeakerArr::kMono, quad = SpeakerArr::k40Cine;
        check(processor->setBusArrangements(nullptr, 0, &mono, 1) == kResultTrue, "mono output accepted");
        check(processor->setBusArrangements(nullptr, 0, &quad, 1) != kResultTrue, "4-channel output refused");
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        check(processor->setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue, "stereo output accepted");

        // ---- parameters ---------------------------------------------------------------
        check(controller->getParameterCount() == 1 + 16 * 130, "bypass + 2080 MIDI controller parameters (0.5.1)");
        ParameterInfo parameter {};
        controller->getParameterInfo(0, parameter);
        check(parameter.id == 0x62797073 && (parameter.flags & ParameterInfo::kIsBypass), "bypass is 'byps'");
        ParamID sustainId = 0;
        check(mapping && mapping->getMidiControllerAssignment(0, 0, 64, sustainId) == kResultTrue
                  && sustainId == 0x6d636d00 + 64,
              "IMidiMapping: CC64 on channel 1 maps to 0x6d636d40");

        // ---- 0.5.1 state ----------------------------------------------------------------
        const auto legacy = readFile(fixtures + "/State051/vst3-custom.state");
        check(!legacy.empty() && setStateBytes(component, legacy), "0.5.1's VST3 state is accepted");
        const auto restored = stateOf(component);
        for (const char* expected : { "keyIndex=\"3\"", "scale=\"dorian\"", "chordPreset=\"neoSoul\"",
                                      "transposeTarget=\"lowest\"", "transposeOctave=\"-1\"",
                                      "velocityMode=\"fixed\"", "fixedVelocity=\"77\"",
                                      "packSlug=\"chordengine-test-pack\"", "packName=\"Test &amp; Pack\"",
                                      "internalAudio=\"on\"", "internalSound=\"bass\"",
                                      "recordQuantize=\"thirdBeat\"", "followHostRecord=\"on\"" })
            check(contains(restored, expected), std::string("restored from 0.5.1: ") + expected);
        check(!contains(restored, "JUCEPrivateData"), "this build writes no private-data trailer");
        {
            auto second = factory.createInstance<IComponent>(processorClass.ID());
            second->initialize(&host);
            check(setStateBytes(second, restored) && stateOf(second) == restored,
                  "this build's state reloads into a new instance unchanged");
            second->terminate();
        }
        check(setStateBytes(component, readFile(fixtures + "/State051/vst3-default.state"))
                  && contains(stateOf(component), "chordPreset=\"dreamy\""),
              "0.5.1's default state restores the defaults");

        // ---- MIDI through process() -----------------------------------------------------------
        ProcessSetup setup { kRealtime, kSample32, 512, 48000.0 };
        processor->setupProcessing(setup);
        component->setActive(true);
        processor->setProcessing(true);

        HostProcessData data;
        data.prepare(*component, 512, kSample32);
        EventList inputEvents, outputEvents;
        ParameterChanges inputChanges;
        ProcessContext context {};
        context.state = ProcessContext::kTempoValid;
        context.tempo = 120.0;
        context.sampleRate = 48000.0;
        data.inputEvents = &inputEvents;
        data.outputEvents = &outputEvents;
        data.inputParameterChanges = &inputChanges;
        data.processContext = &context;
        data.numSamples = 512;

        auto countNoteOns = [&outputEvents]
        {
            int ons = 0;
            for (int32 i = 0; i < outputEvents.getEventCount(); ++i)
            {
                Event event {};
                outputEvents.getEvent(i, event);
                ons += event.type == Event::kNoteOnEvent ? 1 : 0;
            }
            return ons;
        };

        auto armProbe = reinterpret_cast<ArmFn>(dlsym(RTLD_DEFAULT, "ceRtProbeArm"));
        auto allocations = reinterpret_cast<CountFn>(dlsym(RTLD_DEFAULT, "ceRtProbeAllocations"));
        auto locks = reinterpret_cast<CountFn>(dlsym(RTLD_DEFAULT, "ceRtProbeLocks"));

        Event noteOn {};
        noteOn.type = Event::kNoteOnEvent;
        noteOn.sampleOffset = 10;
        noteOn.noteOn.channel = 0;
        noteOn.noteOn.pitch = 60;
        noteOn.noteOn.velocity = 100.0f / 127.0f;
        noteOn.noteOn.noteId = -1;
        inputEvents.addEvent(noteOn);
        // Warm-up block (outside any probe): the first call may touch lazily
        // initialised host-side helpers.
        processor->process(data);
        check(countNoteOns() == 4, "a middle C trigger produces a four-note chord on the event output ("
                                       + std::to_string(countNoteOns()) + ")");

        long allocationsBefore = allocations ? allocations() : 0, locksBefore = locks ? locks() : 0;
        for (int block = 0; block < 64; ++block)
        {
            inputEvents.clear();
            outputEvents.clear();
            inputChanges.clearQueue();
            Event event {};
            event.type = block % 2 == 0 ? Event::kNoteOnEvent : Event::kNoteOffEvent;
            event.sampleOffset = block;
            event.noteOn.channel = 0;
            event.noteOn.pitch = static_cast<int16>(48 + block % 24);
            event.noteOn.velocity = 0.8f;
            event.noteOn.noteId = -1;
            inputEvents.addEvent(event);
            int32 index = 0;
            if (auto* queue = inputChanges.addParameterData(sustainId, index))
            {
                int32 point = 0;
                queue->addPoint(5, block % 4 < 2 ? 1.0 : 0.0, point);
            }
            if (armProbe != nullptr)
                armProbe(1);
            processor->process(data);
            if (armProbe != nullptr)
                armProbe(0);
        }
        if (armProbe != nullptr)
            check(allocations() == allocationsBefore && locks() == locksBefore,
                  "process(): no allocation and no lock in 64 blocks with notes and sustain ("
                      + std::to_string(allocations() - allocationsBefore) + " allocations, "
                      + std::to_string(locks() - locksBefore) + " locks)");
        else
            std::printf("NOTE real-time probe not injected; the RT check runs in the CTest variant\n");

        processor->setProcessing(false);
        component->setActive(false);
        component->terminate();
        std::printf("%s: %d failure(s)\n", failures == 0 ? "VST3 HOST TEST PASSED" : "VST3 HOST TEST FAILED", failures);
        return failures == 0 ? 0 : 1;
    }
}
