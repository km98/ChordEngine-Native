// =============================================================================
// ChordEngineVST3.mm - ChordEngine as a VST3 plug-in on the Steinberg VST 3 SDK.
//
// One class is both the audio component and the edit controller (the
// single-component pattern), so the controller and the processor share one
// ChordEngineAudioProcessor without a private side channel.
//
// Identity is 0.5.1's, so FL Studio / Ableton / Cubase projects load this
// build: processor class ID ABCDEF01 9182FAEB 'Musc' 'Cefl', name
// "ChordEngine", vendor Music-Prod, category Instrument. The parameter IDs
// 0.5.1 exposed are kept: the bypass parameter 'byps' and the hidden MIDI
// controller parameters at 0x6d636d00 + channel * 130 + controller, which
// the host feeds through IMidiMapping (VST3 has no MIDI CC events).
//
// State: 0.5.1 wrote the plug-in's XML followed by a private-data trailer;
// that trailer is stripped on load. This build writes the XML only.
//
// Buses: event input "MIDI Input", event output "MIDI Output", audio output
// "Output" (stereo; the internal sound).
// =============================================================================
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#define CHORDENGINE_VST3_EXPORT __attribute__((visibility("default")))
#else
#define CHORDENGINE_VST3_EXPORT __declspec(dllexport)
#endif

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/vst/vstbus.h"
#include "public.sdk/source/vst/vstcomponent.h"

#include "Platform/Platform.h"
#include "PluginProcessor.h"
#include "UI/EditorHost.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

// Defined in the SDK's main/ sources, which this plug-in does not compile.
namespace Steinberg
{
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
} // namespace Steinberg

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
// 0.5.1's class ID: ABCDEF01 9182FAEB <manufacturer 'Musc'> <plug-in code 'Cefl'>.
const FUID kProcessorUID(0xABCDEF01, 0x9182FAEB, 0x4D757363, 0x4365666C);

constexpr const char* kVendor = "Music-Prod";
constexpr const char* kName = "ChordEngine";
#ifdef CHORDENGINE_PLUGIN_VERSION_STRING
constexpr const char* kVersion = CHORDENGINE_PLUGIN_VERSION_STRING;
#else
constexpr const char* kVersion = "0.0.0";
#endif

// 0.5.1's parameter IDs.
constexpr ParamID kBypassId = 0x62797073;           // 'byps'
constexpr ParamID kMidiControllerBase = 0x6d636d00; // 'mdm\0'
constexpr int kControllersPerChannel = kCountCtrlNumber; // 128 CCs + aftertouch + pitch bend
constexpr int kMidiControllerParameters = 16 * kControllersPerChannel;

void copyTitle(String128 destination, const std::string& text)
{
    UString(destination, 128).fromAscii(text.c_str());
}

std::string readAll(IBStream* stream)
{
    std::string out;
    if (stream == nullptr)
        return out;
    char buffer[4096];
    int32 got = 0;
    while (stream->read(buffer, sizeof(buffer), &got) == kResultOk && got > 0)
    {
        out.append(buffer, static_cast<std::size_t>(got));
        if (out.size() > 16u * 1024u * 1024u)
            break;
    }
    return out;
}

// Strips the private-data trailer a 0.5.x state carries after the plug-in
// state: an int64 0, a value tree, its int64 size, "JUCEPrivateData". Format
// knowledge only; nothing of it is kept.
void stripPrivateDataTrailer(std::string& s)
{
    static const std::string id = "JUCEPrivateData";
    if (s.size() < id.size() + 16 || s.compare(s.size() - id.size(), id.size(), id) != 0)
        return;
    std::uint64_t privateSize = 0;
    std::memcpy(&privateSize, s.data() + s.size() - id.size() - 8, 8);
    const std::uint64_t tail = privateSize + id.size() + 16;
    if (tail <= s.size())
        s.resize(static_cast<std::size_t>(s.size() - tail));
}

class ChordEngineVST3;

class ChordEngineVST3View : public CPluginView
{
public:
    explicit ChordEngineVST3View(ChordEngineVST3& owner);
    ~ChordEngineVST3View() override;

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
#ifdef __APPLE__
        return std::strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
#else
        return kResultFalse; // the Windows editor is planned (Docs/NATIVE_REBUILD.md)
#endif
    }
    tresult PLUGIN_API attached(void* parent, FIDString type) override;
    tresult PLUGIN_API removed() override;
    tresult PLUGIN_API canResize() override { return kResultTrue; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) override
    {
        if (rect == nullptr)
            return kInvalidArgument;
        int width = 0, height = 0;
        constrain(rect->getWidth(), width, height);
        rect->right = rect->left + width;
        rect->bottom = rect->top + height;
        return kResultTrue;
    }
    tresult PLUGIN_API onSize(ViewRect* rect) override
    {
        if (rect == nullptr)
            return kInvalidArgument;
        ViewRect fitted = *rect;
        checkSizeConstraint(&fitted);
#ifdef __APPLE__
        if (view_ != nil)
            [view_ setFrame:NSMakeRect(0, 0, fitted.getWidth(), fitted.getHeight())];
#endif
        return CPluginView::onSize(&fitted);
    }
    bool resizeTo(int width, int height)
    {
        ViewRect rect(0, 0, width, height);
        checkSizeConstraint(&rect);
        return plugFrame != nullptr && plugFrame->resizeView(this, &rect) == kResultTrue;
    }

    static void constrain(int requestedWidth, int& width, int& height)
    {
        width = std::clamp(requestedWidth, chordengine::ui::editorMinimumWidth, chordengine::ui::editorMaximumWidth);
        height = static_cast<int>(std::lround(width * static_cast<double>(chordengine::ui::editorDesignHeight)
                                              / chordengine::ui::editorDesignWidth));
    }

private:
    ChordEngineVST3& owner_;
#ifdef __APPLE__
    NSView* view_ = nil;
#endif
};

class ChordEngineVST3 final : public Steinberg::Vst::Component,
                              public IAudioProcessor,
                              public IEditController,
                              public IMidiMapping,
                              public IProcessContextRequirements,
                              public chordengine::ui::EditorHost
{
public:
    ChordEngineVST3() : processor_(true)
    {
        events_.reserve(ChordEngineAudioProcessor::midiEventCapacity);
    }

    static FUnknown* create(void*) { return static_cast<IAudioProcessor*>(new ChordEngineVST3()); }

    // ---- IPluginBase / IComponent -----------------------------------------------------
    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult result = Steinberg::Vst::Component::initialize(context);
        if (result != kResultOk)
            return result;
        // The host initialises plug-ins on its UI thread.
        chordengine::platform::initialiseMainThread();
        audioOutputs.push_back(owned(new AudioBus(STR16("Output"), kMain, BusInfo::kDefaultActive, SpeakerArr::kStereo)));
        eventInputs.push_back(owned(new EventBus(STR16("MIDI Input"), kMain, BusInfo::kDefaultActive, 16)));
        eventOutputs.push_back(owned(new EventBus(STR16("MIDI Output"), kMain, BusInfo::kDefaultActive, 16)));
        return kResultOk;
    }

    tresult PLUGIN_API terminate() override { return Steinberg::Vst::Component::terminate(); }

    tresult PLUGIN_API getControllerClassId(TUID) override { return kNotImplemented; } // single component

    tresult PLUGIN_API setState(IBStream* stream) override
    {
        std::string bytes = readAll(stream);
        if (bytes.empty())
            return kResultFalse;
        stripPrivateDataTrailer(bytes);
        // An unreadable state (for example a controller state some hosts pass
        // to a single-component plug-in) changes nothing.
        if (const auto state = chordengine::state::deserialize(bytes.data(), bytes.size()))
        {
            processor_.applyState(*state);
            bypassed_ = state->hostBypass.value_or(false);
        }
        if (handler_ != nullptr)
            handler_->restartComponent(kParamValuesChanged);
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream* stream) override
    {
        if (stream == nullptr)
            return kInvalidArgument;
        auto snapshot = processor_.stateSnapshot();
        snapshot.hostBypass = bypassed_;
        const std::string state = chordengine::state::serialize(snapshot);
        int32 written = 0;
        return stream->write(const_cast<char*>(state.data()), static_cast<int32>(state.size()), &written) == kResultOk
                       && written == static_cast<int32>(state.size())
                   ? kResultOk
                   : kResultFalse;
    }

    tresult PLUGIN_API setActive(TBool) override { return kResultOk; }

    // ---- IAudioProcessor ------------------------------------------------------------------
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32 numIns, SpeakerArrangement* outs, int32 numOuts) override
    {
        if (numIns != 0 || numOuts != 1)
            return kResultFalse;
        // Mono or stereo output; anything else is refused.
        if (outs[0] != SpeakerArr::kStereo && outs[0] != SpeakerArr::kMono)
            return kResultFalse;
        if (auto* bus = FCast<AudioBus>(audioOutputs.at(0)))
            bus->setArrangement(outs[0]);
        return kResultTrue;
    }

    tresult PLUGIN_API getBusArrangement(BusDirection direction, int32 index, SpeakerArrangement& arrangement) override
    {
        if (direction == kOutput && index == 0)
            if (auto* bus = FCast<AudioBus>(audioOutputs.at(0)))
            {
                arrangement = bus->getArrangement();
                return kResultTrue;
            }
        return kInvalidArgument;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    uint32 PLUGIN_API getTailSamples() override
    {
        return static_cast<uint32>(std::lround(processor_.getTailLengthSeconds() * sampleRate_));
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override
    {
        if (setup.symbolicSampleSize != kSample32)
            return kResultFalse;
        sampleRate_ = setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0;
        processor_.prepareToPlay(sampleRate_, setup.maxSamplesPerBlock);
        events_.reserve(ChordEngineAudioProcessor::midiEventCapacity);
        return kResultOk;
    }

    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        events_.clear();

        // Parameter changes: bypass, and the MIDI controller parameters the
        // host derived from its MIDI input (IMidiMapping).
        if (auto* changes = data.inputParameterChanges)
            for (int32 q = 0; q < changes->getParameterCount(); ++q)
                if (auto* queue = changes->getParameterData(q))
                    readParameterQueue(*queue);

        if (auto* input = data.inputEvents)
            for (int32 index = 0; index < input->getEventCount(); ++index)
            {
                Event event {};
                if (input->getEvent(index, event) != kResultOk)
                    continue;
                addInputEvent(event);
            }

        HostTransport transport;
        if (auto* context = data.processContext)
        {
            transport.isPlaying = (context->state & ProcessContext::kPlaying) != 0;
            transport.isRecording = (context->state & ProcessContext::kRecording) != 0;
            if (context->state & ProcessContext::kTempoValid)
                transport.bpm = context->tempo;
            if (context->state & ProcessContext::kProjectTimeMusicValid)
                transport.ppqPosition = context->projectTimeMusic;
            if (context->state & ProcessContext::kBarPositionValid)
                transport.ppqPositionOfLastBarStart = context->barPositionMusic;
            if (context->state & ProcessContext::kTimeSigValid)
            {
                transport.timeSignatureNumerator = context->timeSigNumerator;
                transport.timeSignatureDenominator = context->timeSigDenominator;
            }
        }

        std::array<float*, 2> channels {};
        int channelCount = 0;
        if (data.numOutputs > 0 && data.outputs[0].channelBuffers32 != nullptr)
        {
            channelCount = std::min<int32>(data.outputs[0].numChannels, 2);
            for (int c = 0; c < channelCount; ++c)
                channels[static_cast<std::size_t>(c)] = data.outputs[0].channelBuffers32[c];
        }
        const int samples = std::max<int32>(0, data.numSamples);

        if (bypassed_)
        {
            // Bypassed: the input MIDI passes through and the output is silent.
            for (int c = 0; c < channelCount; ++c)
                std::fill(channels[static_cast<std::size_t>(c)], channels[static_cast<std::size_t>(c)] + samples, 0.0f);
        }
        else if (samples > 0)
            processor_.processBlock({ channels.data(), channelCount, samples }, events_, transport);
        else if (!events_.empty())
            processor_.processBlock({ channels.data(), 0, 0 }, events_, transport);

        if (data.numOutputs > 0)
            data.outputs[0].silenceFlags = processor_.internalAudioEnabled() ? 0 : ((1ull << channelCount) - 1);

        if (auto* output = data.outputEvents)
            for (const auto& event : events_)
                writeOutputEvent(*output, event);
        return kResultOk;
    }

    uint32 PLUGIN_API getProcessContextRequirements() override
    {
        return kNeedProjectTimeMusic | kNeedBarPositionMusic | kNeedTempo | kNeedTimeSignature | kNeedTransportState;
    }

    // ---- IEditController ------------------------------------------------------------------
    tresult PLUGIN_API setComponentState(IBStream*) override { return kResultOk; }
    int32 PLUGIN_API getParameterCount() override { return 1 + kMidiControllerParameters; }

    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo& info) override
    {
        if (index < 0 || index >= getParameterCount())
            return kInvalidArgument;
        info = {};
        info.unitId = kRootUnitId;
        if (index == 0)
        {
            info.id = kBypassId;
            copyTitle(info.title, "Bypass");
            copyTitle(info.shortTitle, "Bypass");
            info.stepCount = 1;
            info.defaultNormalizedValue = 0.0;
            info.flags = ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass;
            return kResultOk;
        }
        const int controller = index - 1;
        info.id = kMidiControllerBase + static_cast<ParamID>(controller);
        copyTitle(info.title, "MIDI CC " + std::to_string(controller / kControllersPerChannel) + "|"
                                  + std::to_string(controller % kControllersPerChannel));
        info.flags = 0; // hidden from automation, as in 0.5.1
        return kResultOk;
    }

    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue value, String128 text) override
    {
        if (id == kBypassId)
        {
            copyTitle(text, value >= 0.5 ? "On" : "Off");
            return kResultOk;
        }
        if (isControllerParameter(id))
        {
            copyTitle(text, std::to_string(static_cast<int>(std::lround(value * 127.0))));
            return kResultOk;
        }
        return kInvalidArgument;
    }

    tresult PLUGIN_API getParamValueByString(ParamID id, TChar* text, ParamValue& value) override
    {
        char ascii[128] = {};
        UString(text, 128).toAscii(ascii, sizeof(ascii));
        if (id == kBypassId)
        {
            value = (std::strcmp(ascii, "On") == 0 || std::atof(ascii) >= 0.5) ? 1.0 : 0.0;
            return kResultOk;
        }
        return kInvalidArgument;
    }

    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue value) override { return value; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue value) override { return value; }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return id == kBypassId ? (bypassed_ ? 1.0 : 0.0) : 0.0; }

    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override
    {
        if (id == kBypassId)
        {
            bypassed_ = value >= 0.5;
            return kResultOk;
        }
        return isControllerParameter(id) ? kResultOk : kInvalidArgument;
    }

    tresult PLUGIN_API setComponentHandler(IComponentHandler* handler) override
    {
        if (handler != nullptr)
            handler->addRef();
        if (handler_ != nullptr)
            handler_->release();
        handler_ = handler;
        return kResultOk;
    }

    IPlugView* PLUGIN_API createView(FIDString name) override
    {
        return (name != nullptr && std::strcmp(name, ViewType::kEditor) == 0) ? new ChordEngineVST3View(*this) : nullptr;
    }

    // ---- IMidiMapping ---------------------------------------------------------------------
    tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 channel, CtrlNumber controller, ParamID& id) override
    {
        if (busIndex != 0 || channel < 0 || channel >= 16 || controller < 0 || controller >= kControllersPerChannel)
            return kResultFalse;
        id = kMidiControllerBase + static_cast<ParamID>(channel * kControllersPerChannel + controller);
        return kResultTrue;
    }

    // ---- EditorHost -------------------------------------------------------------------------
    ChordEngineAudioProcessor& processor() override { return processor_; }
    bool requestEditorSize(int width, int height) override
    {
        return activeView_ != nullptr && activeView_->resizeTo(width, height);
    }
    std::string hostName() const override
    {
#ifdef __APPLE__
        NSString* name = NSProcessInfo.processInfo.processName;
        if ([name containsString:@"FL Studio"] || [name containsString:@"OsxFL"])
            return "FL Studio";
        if ([name containsString:@"Live"])
            return "Ableton Live";
#endif
        return {};
    }

    ChordEngineVST3View* activeView_ = nullptr;

    // ---- FUnknown ---------------------------------------------------------------------------
    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override
    {
        QUERY_INTERFACE(iid, object, IAudioProcessor::iid, IAudioProcessor)
        QUERY_INTERFACE(iid, object, IEditController::iid, IEditController)
        QUERY_INTERFACE(iid, object, IMidiMapping::iid, IMidiMapping)
        QUERY_INTERFACE(iid, object, IProcessContextRequirements::iid, IProcessContextRequirements)
        return Steinberg::Vst::Component::queryInterface(iid, object);
    }
    REFCOUNT_METHODS(Steinberg::Vst::Component)

private:
    ~ChordEngineVST3() override
    {
#ifdef __APPLE__
        chordengine::ui::detachAllEditorViews(this);
#endif
        if (handler_ != nullptr)
            handler_->release();
    }

    static bool isControllerParameter(ParamID id) noexcept
    {
        return id >= kMidiControllerBase && id < kMidiControllerBase + static_cast<ParamID>(kMidiControllerParameters);
    }

    void readParameterQueue(IParamValueQueue& queue)
    {
        const ParamID id = queue.getParameterId();
        const int32 points = queue.getPointCount();
        if (id == kBypassId)
        {
            int32 offset = 0;
            ParamValue value = 0.0;
            if (points > 0 && queue.getPoint(points - 1, offset, value) == kResultOk)
                bypassed_ = value >= 0.5;
            return;
        }
        if (!isControllerParameter(id))
            return;
        const int index = static_cast<int>(id - kMidiControllerBase);
        const int channel = index / kControllersPerChannel;
        const int controller = index % kControllersPerChannel;
        for (int32 point = 0; point < points; ++point)
        {
            int32 offset = 0;
            ParamValue value = 0.0;
            if (queue.getPoint(point, offset, value) != kResultOk)
                continue;
            using chordengine::midi::MidiEvent;
            const auto status = static_cast<std::uint8_t>(channel & 0x0F);
            if (controller == kAfterTouch)
                events_.add(MidiEvent::make(offset, static_cast<std::uint8_t>(0xD0 | status),
                                            static_cast<std::uint8_t>(std::lround(value * 127.0)), 0, 2));
            else if (controller == kPitchBend)
            {
                const int bend = std::clamp(static_cast<int>(std::lround(value * 16383.0)), 0, 16383);
                events_.add(MidiEvent::make(offset, static_cast<std::uint8_t>(0xE0 | status),
                                            static_cast<std::uint8_t>(bend & 0x7F),
                                            static_cast<std::uint8_t>((bend >> 7) & 0x7F)));
            }
            else if (controller < 128)
                events_.add(MidiEvent::controller(offset, channel + 1, controller,
                                                  static_cast<int>(std::lround(value * 127.0))));
        }
    }

    void addInputEvent(const Event& event)
    {
        using chordengine::midi::MidiEvent;
        switch (event.type)
        {
            case Event::kNoteOnEvent:
            {
                const int velocity = static_cast<int>(std::lround(event.noteOn.velocity * 127.0f));
                if (velocity <= 0)
                    events_.add(MidiEvent::noteOff(event.sampleOffset, event.noteOn.channel + 1, event.noteOn.pitch));
                else
                    events_.add(MidiEvent::noteOn(event.sampleOffset, event.noteOn.channel + 1, event.noteOn.pitch,
                                                  std::min(127, velocity)));
                break;
            }
            case Event::kNoteOffEvent:
                events_.add(MidiEvent::noteOff(event.sampleOffset, event.noteOff.channel + 1, event.noteOff.pitch,
                                               static_cast<int>(std::lround(event.noteOff.velocity * 127.0f))));
                break;
            case Event::kPolyPressureEvent:
                events_.add(MidiEvent::make(event.sampleOffset,
                                            static_cast<std::uint8_t>(0xA0 | (event.polyPressure.channel & 0x0F)),
                                            static_cast<std::uint8_t>(event.polyPressure.pitch & 0x7F),
                                            static_cast<std::uint8_t>(std::lround(event.polyPressure.pressure * 127.0f))));
                break;
            case Event::kLegacyMIDICCOutEvent:
                break;
            default:
                break;
        }
    }

    static void writeOutputEvent(IEventList& output, const chordengine::midi::MidiEvent& message)
    {
        Event event {};
        event.busIndex = 0;
        event.sampleOffset = message.sampleOffset;
        event.flags = Event::kIsLive;
        const int channel = message.channel() - 1;
        if (message.isNoteOn())
        {
            event.type = Event::kNoteOnEvent;
            event.noteOn.channel = static_cast<int16>(channel);
            event.noteOn.pitch = static_cast<int16>(message.noteNumber());
            event.noteOn.velocity = static_cast<float>(message.velocity()) / 127.0f;
            event.noteOn.noteId = -1;
        }
        else if (message.isNoteOff())
        {
            event.type = Event::kNoteOffEvent;
            event.noteOff.channel = static_cast<int16>(channel);
            event.noteOff.pitch = static_cast<int16>(message.noteNumber());
            event.noteOff.velocity = static_cast<float>(message.velocity()) / 127.0f;
            event.noteOff.noteId = -1;
        }
        else
        {
            const auto type = message.status() & 0xF0;
            event.type = Event::kLegacyMIDICCOutEvent;
            event.midiCCOut.channel = static_cast<int8>(channel);
            if (type == 0xB0)
            {
                event.midiCCOut.controlNumber = static_cast<uint8>(message.bytes[1]);
                event.midiCCOut.value = static_cast<int8>(message.bytes[2]);
            }
            else if (type == 0xE0)
            {
                event.midiCCOut.controlNumber = kPitchBend;
                event.midiCCOut.value = static_cast<int8>(message.bytes[1]);
                event.midiCCOut.value2 = static_cast<int8>(message.bytes[2]);
            }
            else if (type == 0xD0)
            {
                event.midiCCOut.controlNumber = kAfterTouch;
                event.midiCCOut.value = static_cast<int8>(message.bytes[1]);
            }
            else if (type == 0xC0)
            {
                event.midiCCOut.controlNumber = kCtrlProgramChange;
                event.midiCCOut.value = static_cast<int8>(message.bytes[1]);
            }
            else if (type == 0xA0)
            {
                event.type = Event::kPolyPressureEvent;
                event.polyPressure.channel = static_cast<int16>(channel);
                event.polyPressure.pitch = static_cast<int16>(message.bytes[1]);
                event.polyPressure.pressure = static_cast<float>(message.bytes[2]) / 127.0f;
                event.polyPressure.noteId = -1;
            }
            else
                return;
        }
        output.addEvent(event);
    }

    ChordEngineAudioProcessor processor_;
    chordengine::midi::MidiEventList events_;
    IComponentHandler* handler_ = nullptr;
    double sampleRate_ = 44100.0;
    std::atomic<bool> bypassed_ { false };
};

ChordEngineVST3View::ChordEngineVST3View(ChordEngineVST3& owner) : CPluginView(nullptr), owner_(owner)
{
    owner_.addRef();
    ViewRect rect(0, 0, chordengine::ui::editorDesignWidth, chordengine::ui::editorDesignHeight);
    setRect(rect);
}

ChordEngineVST3View::~ChordEngineVST3View()
{
    removed();
    owner_.release();
}

tresult PLUGIN_API ChordEngineVST3View::attached(void* parent, FIDString type)
{
    if (parent == nullptr || isPlatformTypeSupported(type) != kResultTrue)
        return kResultFalse;
    owner_.activeView_ = this;
#ifdef __APPLE__
    @autoreleasepool
    {
        view_ = chordengine::ui::createEditorView(owner_, getRect().getWidth(), getRect().getHeight());
        [(__bridge NSView*) parent addSubview:view_];
    }
#endif
    return CPluginView::attached(parent, type);
}

tresult PLUGIN_API ChordEngineVST3View::removed()
{
    if (owner_.activeView_ == this)
        owner_.activeView_ = nullptr;
#ifdef __APPLE__
    if (view_ != nil)
    {
        chordengine::ui::detachEditorView(view_);
        [view_ removeFromSuperview];
        view_ = nil;
    }
#endif
    return CPluginView::removed();
}

// ---- the factory ---------------------------------------------------------------------------------
class ChordEngineFactory : public IPluginFactory3
{
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override
    {
        QUERY_INTERFACE(iid, object, FUnknown::iid, IPluginFactory)
        QUERY_INTERFACE(iid, object, IPluginFactory::iid, IPluginFactory)
        QUERY_INTERFACE(iid, object, IPluginFactory2::iid, IPluginFactory2)
        QUERY_INTERFACE(iid, object, IPluginFactory3::iid, IPluginFactory3)
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override
    {
        *info = PFactoryInfo(kVendor, "https://music-prod.com", "", PFactoryInfo::kUnicode);
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return 1; }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override
    {
        if (index != 0)
            return kInvalidArgument;
        *info = PClassInfo(kProcessorUID, PClassInfo::kManyInstances, kVstAudioEffectClass, kName);
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override
    {
        if (index != 0)
            return kInvalidArgument;
        *info = PClassInfo2(kProcessorUID, PClassInfo::kManyInstances, kVstAudioEffectClass, kName,
                            Vst::kSimpleModeSupported, "Instrument", kVendor, kVersion, kVstVersionString);
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override
    {
        PClassInfo2 ascii;
        if (getClassInfo2(index, &ascii) != kResultOk)
            return kInvalidArgument;
        *info = PClassInfoW();
        info->fromAscii(ascii);
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** object) override
    {
        *object = nullptr;
        if (!FUnknownPrivate::iidEqual(cid, kProcessorUID.toTUID()))
            return kNoInterface;
        FUnknown* instance = ChordEngineVST3::create(nullptr);
        const tresult result = instance->queryInterface(iid, object);
        instance->release();
        return result;
    }
    tresult PLUGIN_API setHostContext(FUnknown*) override { return kResultOk; }
};
} // namespace

extern "C"
{
CHORDENGINE_VST3_EXPORT IPluginFactory* PLUGIN_API GetPluginFactory()
{
    static ChordEngineFactory factory;
    return &factory;
}
#ifdef __APPLE__
CHORDENGINE_VST3_EXPORT bool bundleEntry(CFBundleRef) { return true; }
CHORDENGINE_VST3_EXPORT bool bundleExit() { return true; }
#else
CHORDENGINE_VST3_EXPORT bool InitDll() { return true; }
CHORDENGINE_VST3_EXPORT bool ExitDll() { return true; }
#endif
}
