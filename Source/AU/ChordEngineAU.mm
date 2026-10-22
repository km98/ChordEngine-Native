// =============================================================================
// ChordEngineAU.mm - ChordEngine as an Audio Unit (v2) on Apple's AudioUnitSDK.
//
// One source, two components (CMake compiles it per bundle):
//   CHORDENGINE_AU_INSTRUMENT=0  "Music-Prod: ChordEngine"            aumi/Cefl/Musc
//                                Logic Pro MIDI FX, factory ChordEngineAUFactory
//   CHORDENGINE_AU_INSTRUMENT=1  "Music-Prod: ChordEngine Instrument" aumu/Cein/Musc
//                                instrument with internal audio,
//                                factory ChordEngine_InstrumentAUFactory
// The type/subtype/manufacturer codes, names and factory names are those of
// 0.5.1, so Logic projects saved with 0.5.1 load this build.
//
// State: the ClassInfo dictionary carries the plug-in state under
// "jucePluginState" - the key 0.5.1 (built with JUCE's AU wrapper) wrote - as
// the UTF-8 XML document State/PluginState writes.
// MIDI out: kAudioUnitProperty_MIDIOutputCallback (MIDIPacketList) and, where
// the host offers it, kAudioUnitProperty_MIDIOutputEventListCallback (UMP).
// =============================================================================
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>
#import <CoreMIDI/CoreMIDI.h>
#include <dlfcn.h>

#include "AudioUnitSDK/AUPlugInDispatch.h"
#include "AudioUnitSDK/MusicDeviceBase.h"

#include "Platform/ObjCNames.h"
#include "PluginProcessor.h"
#include "UI/EditorHost.h"

#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#ifndef CHORDENGINE_AU_INSTRUMENT
 #define CHORDENGINE_AU_INSTRUMENT 0
#endif

#if CHORDENGINE_AU_INSTRUMENT
 #define CHORDENGINE_AU_CLASS ChordEngine_InstrumentAU
#else
 #define CHORDENGINE_AU_CLASS ChordEngineAU
#endif

#define ChordEngineAUViewFactory CE_OBJC(ChordEngineAUViewFactory)

namespace
{
// Private property: the in-process EditorHost* (the Cocoa view asks for it).
constexpr AudioUnitPropertyID kEditorHostProperty = 64001;
CFStringRef const kStateKey = CFSTR("jucePluginState");
constexpr UInt32 kMaxFramesDefault = 4096;

// Removes the private-data block a 0.5.x VST3 state carries after the plug-in
// state (an int64 0, a value tree, its int64 size, "JUCEPrivateData"). AU
// states never had it; it is stripped defensively, as format knowledge only.
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
} // namespace

class CHORDENGINE_AU_CLASS final : public ausdk::MusicDeviceBase, public chordengine::ui::EditorHost
{
public:
    explicit CHORDENGINE_AU_CLASS(AudioComponentInstance instance)
        : ausdk::MusicDeviceBase(instance, 0, 1), processor_(CHORDENGINE_AU_INSTRUMENT != 0)
    {
        input_.reserve(ChordEngineAudioProcessor::midiEventCapacity);
    }

    ~CHORDENGINE_AU_CLASS() override
    {
        // A host may dispose the AU while its Cocoa view is still open: the
        // view must stop using it first.
        if ([NSThread isMainThread])
            chordengine::ui::detachAllEditorViews(this);
        else
            dispatch_sync(dispatch_get_main_queue(), ^{ chordengine::ui::detachAllEditorViews(this); });
    }

    void PostConstructor() override
    {
        ausdk::MusicDeviceBase::PostConstructor(); // creates the output bus
        Output(0).SetName(CFSTR("Output"));
    }

    // ---- lifecycle ----------------------------------------------------------
    OSStatus Initialize() override
    {
        // Only the channel counts SupportedNumChannels reports: MusicDeviceBase
        // does not check them itself, and a host must find that what is
        // reported and what is accepted agree.
        const UInt32 channels = Output(0).GetStreamFormat().mChannelsPerFrame;
        if (channels != 1 && channels != 2)
            return kAudioUnitErr_FormatNotSupported;
        const OSStatus result = ausdk::MusicDeviceBase::Initialize();
        if (result != noErr)
            return result;
        const auto maxFrames = static_cast<int>(GetMaxFramesPerSlice() > 0 ? GetMaxFramesPerSlice() : kMaxFramesDefault);
        processor_.prepareToPlay(Output(0).GetStreamFormat().mSampleRate, maxFrames);
        input_.reserve(ChordEngineAudioProcessor::midiEventCapacity);
        packetBuffer_.assign(64 * 1024, 0);
        eventListBuffer_.assign(64 * 1024, 0);
        return noErr;
    }

    void Cleanup() override
    {
        processor_.releaseResources();
        ausdk::MusicDeviceBase::Cleanup();
    }

    OSStatus Reset(AudioUnitScope scope, AudioUnitElement element) override
    {
        queued_.store(0, std::memory_order_release);
        return ausdk::MusicDeviceBase::Reset(scope, element);
    }

    bool CanScheduleParameters() const override { return false; }
    bool StreamFormatWritable(AudioUnitScope, AudioUnitElement) override { return !IsInitialized(); }

    UInt32 SupportedNumChannels(const AUChannelInfo** outInfo) override
    {
        // No input; a mono or a stereo output. The MIDI FX renders silence
        // there (Logic takes no audio from a MIDI FX slot); the instrument
        // plays the internal sound.
        static const AUChannelInfo info[] = { { 0, 1 }, { 0, 2 } };
        if (outInfo != nullptr)
            *outInfo = info;
        return 2;
    }

    bool ValidFormat(AudioUnitScope scope, AudioUnitElement element, const AudioStreamBasicDescription& format) override
    {
        if (scope == kAudioUnitScope_Output && format.mChannelsPerFrame != 1 && format.mChannelsPerFrame != 2)
            return false;
        return ausdk::MusicDeviceBase::ValidFormat(scope, element, format);
    }

    bool SupportsTail() override { return true; }
    Float64 GetTailTime() override { return processor_.getTailLengthSeconds(); }

    // ---- MIDI in -------------------------------------------------------------
    OSStatus HandleMIDIEvent(UInt8 status, UInt8 channel, UInt8 data1, UInt8 data2, UInt32 frame) override
    {
        const int write = queued_.load(std::memory_order_relaxed);
        if (write < static_cast<int>(queue_.size()))
        {
            const auto statusByte = static_cast<std::uint8_t>((status & 0xF0) | (channel & 0x0F));
            const auto type = statusByte & 0xF0;
            const std::uint8_t length = (type == 0xC0 || type == 0xD0) ? 2 : 3;
            queue_[static_cast<std::size_t>(write)] = chordengine::midi::MidiEvent::make(
                static_cast<int>(frame), statusByte, data1, data2, length);
            queued_.store(write + 1, std::memory_order_release);
        }
        return noErr;
    }

    // ---- render ----------------------------------------------------------------
    OSStatus Render(AudioUnitRenderActionFlags& flags, const AudioTimeStamp& timeStamp, UInt32 frames) override
    {
        AudioBufferList& bufferList = Output(0).GetBufferList();
        std::array<float*, 2> channels {};
        const int channelCount = static_cast<int>(std::min<UInt32>(bufferList.mNumberBuffers, 2));
        for (int c = 0; c < channelCount; ++c)
            channels[static_cast<std::size_t>(c)] = static_cast<float*>(bufferList.mBuffers[c].mData);

        // MIDI received for this slice, in offset order.
        input_.clear();
        const int count = queued_.load(std::memory_order_acquire);
        for (int index = 0; index < count; ++index)
        {
            auto event = queue_[static_cast<std::size_t>(index)];
            event.sampleOffset = std::min<int>(event.sampleOffset, static_cast<int>(frames) - 1);
            input_.add(event);
        }
        queued_.store(0, std::memory_order_release);

        processor_.processBlock({ channels.data(), channelCount, static_cast<int>(frames) }, input_, readTransport());
        if (CHORDENGINE_AU_INSTRUMENT == 0 || !processor_.internalAudioEnabled())
            flags |= kAudioUnitRenderAction_OutputIsSilence;
        else
            flags &= ~kAudioUnitRenderAction_OutputIsSilence;
        sendMidiOutput(timeStamp);
        return noErr;
    }

    // ---- state -------------------------------------------------------------------
    OSStatus SaveState(CFPropertyListRef* outData) override
    {
        const OSStatus result = ausdk::MusicDeviceBase::SaveState(outData);
        if (result != noErr)
            return result;
        CFMutableDictionaryRef dict = CFDictionaryCreateMutableCopy(nullptr, 0, static_cast<CFDictionaryRef>(*outData));
        CFRelease(*outData);
        const std::string state = processor_.getStateInformation();
        CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(state.data()), static_cast<CFIndex>(state.size()));
        CFDictionarySetValue(dict, kStateKey, data);
        CFRelease(data);
        *outData = dict;
        return noErr;
    }

    OSStatus RestoreState(CFPropertyListRef plist) override
    {
        if (plist == nullptr || CFGetTypeID(plist) != CFDictionaryGetTypeID())
            return kAudioUnitErr_InvalidPropertyValue;
        // As 0.5.1 did: the base class must not load parameters from "data"
        // (ChordEngine has none), and a preset written by an AUv3 (version 1)
        // is read as version 0.
        CFMutableDictionaryRef copy = CFDictionaryCreateMutableCopy(nullptr, 0, static_cast<CFDictionaryRef>(plist));
        CFDictionaryRemoveValue(copy, CFSTR(kAUPresetDataKey));
        if (auto version = static_cast<CFNumberRef>(CFDictionaryGetValue(copy, CFSTR(kAUPresetVersionKey)));
            version != nullptr && CFGetTypeID(version) == CFNumberGetTypeID())
        {
            SInt32 value = 0;
            CFNumberGetValue(version, kCFNumberSInt32Type, &value);
            if (value == 1)
            {
                const SInt32 zero = 0;
                CFNumberRef replacement = CFNumberCreate(nullptr, kCFNumberSInt32Type, &zero);
                CFDictionarySetValue(copy, CFSTR(kAUPresetVersionKey), replacement);
                CFRelease(replacement);
            }
        }
        const OSStatus result = ausdk::MusicDeviceBase::RestoreState(copy);
        CFRelease(copy);
        if (result != noErr)
            return result;

        auto data = static_cast<CFDataRef>(CFDictionaryGetValue(static_cast<CFDictionaryRef>(plist), kStateKey));
        if (data != nullptr && CFGetTypeID(data) == CFDataGetTypeID() && CFDataGetLength(data) > 0)
        {
            std::string bytes(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), static_cast<std::size_t>(CFDataGetLength(data)));
            stripPrivateDataTrailer(bytes);
            processor_.setStateInformation(bytes.data(), bytes.size());
        }
        return noErr;
    }

    // One factory preset, as 0.5.1 reported (hosts may store "preset 0").
    OSStatus GetPresets(CFArrayRef* outData) const override
    {
        if (outData == nullptr)
            return noErr;
        static AUPreset preset { 0, CFSTR("Default") };
        CFMutableArrayRef array = CFArrayCreateMutable(nullptr, 1, nullptr);
        CFArrayAppendValue(array, &preset);
        *outData = array;
        return noErr;
    }

    OSStatus NewFactoryPresetSet(const AUPreset& preset) override
    {
        if (preset.presetNumber != 0)
            return kAudioUnitErr_InvalidPropertyValue;
        SetAFactoryPresetAsCurrent(preset);
        return noErr;
    }

    // ---- properties ----------------------------------------------------------------
    OSStatus GetPropertyInfo(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element,
                             UInt32& size, bool& writable) override
    {
        if (scope == kAudioUnitScope_Global)
        {
            switch (id)
            {
                case kAudioUnitProperty_CocoaUI: size = sizeof(AudioUnitCocoaViewInfo); writable = false; return noErr;
                case kEditorHostProperty: size = sizeof(void*); writable = false; return noErr;
                case kAudioUnitProperty_MIDIOutputCallbackInfo: size = sizeof(CFArrayRef); writable = false; return noErr;
                case kAudioUnitProperty_MIDIOutputCallback: size = sizeof(AUMIDIOutputCallbackStruct); writable = true; return noErr;
                case kAudioUnitProperty_MIDIOutputEventListCallback:
                    if (@available(macOS 12.0, *)) { size = sizeof(AUMIDIEventListBlock); writable = true; return noErr; }
                    break;
                default: break;
            }
        }
        return ausdk::MusicDeviceBase::GetPropertyInfo(id, scope, element, size, writable);
    }

    OSStatus GetProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element, void* out) override
    {
        if (scope == kAudioUnitScope_Global)
        {
            switch (id)
            {
                case kAudioUnitProperty_CocoaUI:
                {
                    Dl_info info {};
                    dladdr(reinterpret_cast<const void*>(&chordengine::ui::createEditorView), &info);
                    NSString* executable = [NSString stringWithUTF8String:info.dli_fname];
                    NSString* bundle = [[[executable stringByDeletingLastPathComponent] stringByDeletingLastPathComponent]
                        stringByDeletingLastPathComponent];
                    auto* view = static_cast<AudioUnitCocoaViewInfo*>(out);
                    view->mCocoaAUViewBundleLocation = static_cast<CFURLRef>(CFBridgingRetain([NSURL fileURLWithPath:bundle]));
                    view->mCocoaAUViewClass[0] = static_cast<CFStringRef>(CFBridgingRetain(@CHORDENGINE_OBJC_STR(ChordEngineAUViewFactory)));
                    return noErr;
                }
                case kEditorHostProperty:
                    *static_cast<void**>(out) = static_cast<chordengine::ui::EditorHost*>(this);
                    return noErr;
                case kAudioUnitProperty_MIDIOutputCallbackInfo:
                {
                    CFStringRef name = CFSTR("MIDI Out");
                    *static_cast<CFArrayRef*>(out) = CFArrayCreate(nullptr, reinterpret_cast<const void**>(&name), 1, &kCFTypeArrayCallBacks);
                    return noErr;
                }
                default: break;
            }
        }
        return ausdk::MusicDeviceBase::GetProperty(id, scope, element, out);
    }

    OSStatus SetProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element,
                         const void* data, UInt32 size) override
    {
        if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_MIDIOutputCallback)
        {
            if (size < sizeof(AUMIDIOutputCallbackStruct))
                return kAudioUnitErr_InvalidPropertyValue;
            midiCallback_ = *static_cast<const AUMIDIOutputCallbackStruct*>(data);
            return noErr;
        }
        if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_MIDIOutputEventListCallback)
        {
            if (@available(macOS 12.0, *))
            {
                if (size < sizeof(AUMIDIEventListBlock))
                    return kAudioUnitErr_InvalidPropertyValue;
                AUMIDIEventListBlock block = *static_cast<const AUMIDIEventListBlock __unsafe_unretained*>(data);
                eventListBlock_ = block;
                return noErr;
            }
        }
        return ausdk::MusicDeviceBase::SetProperty(id, scope, element, data, size);
    }

    // ---- EditorHost ------------------------------------------------------------------
    ChordEngineAudioProcessor& processor() override { return processor_; }
    bool requestEditorSize(int, int) override { return false; } // AU views resize themselves
    std::string hostName() const override
    {
        NSString* name = NSProcessInfo.processInfo.processName;
        if ([name containsString:@"Logic"]) return "Logic";
        if ([name containsString:@"GarageBand"]) return "GarageBand";
        return {};
    }

private:
    HostTransport readTransport()
    {
        HostTransport transport;
        Float64 beat = 0.0, tempo = 0.0;
        if (CallHostBeatAndTempo(&beat, &tempo) == noErr)
        {
            transport.ppqPosition = beat;
            if (tempo > 0.0)
                transport.bpm = tempo;
        }
        UInt32 deltaToNextBeat = 0, denominator = 0;
        Float32 numerator = 0.0f;
        Float64 downBeat = 0.0;
        if (CallHostMusicalTimeLocation(&deltaToNextBeat, &numerator, &denominator, &downBeat) == noErr)
        {
            if (numerator > 0.0f && denominator > 0)
            {
                transport.timeSignatureNumerator = static_cast<int>(numerator);
                transport.timeSignatureDenominator = static_cast<int>(denominator);
            }
            transport.ppqPositionOfLastBarStart = downBeat;
        }
        const auto& callbacks = GetHostCallbackInfo();
        Boolean playing = false, recording = false, changed = false, cycling = false;
        Float64 sample = 0.0, cycleStart = 0.0, cycleEnd = 0.0;
        if (callbacks.transportStateProc2 != nullptr
            && callbacks.transportStateProc2(callbacks.hostUserData, &playing, &recording, &changed, &sample,
                                             &cycling, &cycleStart, &cycleEnd) == noErr)
        {
            transport.isPlaying = playing;
            transport.isRecording = recording;
        }
        else if (CallHostTransportState(&playing, &changed, &sample, &cycling, &cycleStart, &cycleEnd) == noErr)
            transport.isPlaying = playing;
        return transport;
    }

    void sendMidiOutput(const AudioTimeStamp& timeStamp)
    {
        if (input_.empty())
            return;
        // ONE path per host: the MIDI 1.0 event list when the host installed
        // it (macOS installs it alongside a legacy callback, which would
        // otherwise deliver every event twice), else the legacy packet list.
        if (@available(macOS 12.0, *))
        {
            if (eventListBlock_ != nil)
            {
                auto* list = reinterpret_cast<struct MIDIEventList*>(eventListBuffer_.data());
                MIDIEventPacket* packet = MIDIEventListInit(list, kMIDIProtocol_1_0);
                for (const auto& event : input_)
                {
                    // MIDI 1.0 channel voice message as a 32-bit UMP word.
                    const UInt32 word = (0x2u << 28) | (static_cast<UInt32>(event.bytes[0]) << 16)
                        | (static_cast<UInt32>(event.bytes[1]) << 8) | (event.size > 2 ? event.bytes[2] : 0);
                    packet = MIDIEventListAdd(list, eventListBuffer_.size(), packet,
                                              static_cast<MIDITimeStamp>(event.sampleOffset), 1, &word);
                    if (packet == nullptr)
                        break;
                }
                eventListBlock_(static_cast<AUEventSampleTime>(timeStamp.mSampleTime), 0, list);
                return;
            }
        }
        if (midiCallback_.midiOutputCallback != nullptr)
        {
            auto* list = reinterpret_cast<MIDIPacketList*>(packetBuffer_.data());
            MIDIPacket* packet = MIDIPacketListInit(list);
            for (const auto& event : input_)
            {
                packet = MIDIPacketListAdd(list, packetBuffer_.size(), packet,
                                           static_cast<MIDITimeStamp>(event.sampleOffset), event.size, event.bytes);
                if (packet == nullptr)
                    break;
            }
            midiCallback_.midiOutputCallback(midiCallback_.userData, &timeStamp, 0, list);
        }
    }

    ChordEngineAudioProcessor processor_;
    // MIDI from HandleMIDIEvent (render thread) until the next Render.
    std::array<chordengine::midi::MidiEvent, 1024> queue_ {};
    std::atomic<int> queued_ { 0 };
    // The block's events in, replaced by its events out.
    chordengine::midi::MidiEventList input_;
    AUMIDIOutputCallbackStruct midiCallback_ {};
    AUMIDIEventListBlock eventListBlock_ API_AVAILABLE(macos(12.0)) = nil;
    std::vector<std::uint8_t> packetBuffer_;
    std::vector<std::uint8_t> eventListBuffer_;
};

// The SDK's entry macro pastes the class name; expand ours first.
#define CHORDENGINE_AU_ENTRY(Class) AUSDK_COMPONENT_ENTRY(ausdk::AUMusicDeviceFactory, Class)
CHORDENGINE_AU_ENTRY(CHORDENGINE_AU_CLASS)

// ---- the Cocoa view factory ------------------------------------------------------------
@interface ChordEngineAUViewFactory : NSObject <AUCocoaUIBase>
@end

@implementation ChordEngineAUViewFactory
- (unsigned)interfaceVersion
{
    return 0;
}

- (NSString*)description
{
    return @"ChordEngine";
}

- (NSView*)uiViewForAudioUnit:(AudioUnit)unit withSize:(NSSize)size
{
    (void) size;
    void* host = nullptr;
    UInt32 propertySize = sizeof(host);
    if (AudioUnitGetProperty(unit, kEditorHostProperty, kAudioUnitScope_Global, 0, &host, &propertySize) != noErr || host == nullptr)
        return nil;
    return chordengine::ui::createEditorView(*static_cast<chordengine::ui::EditorHost*>(host),
                                             chordengine::ui::editorDesignWidth,
                                             chordengine::ui::editorDesignHeight);
}
@end
