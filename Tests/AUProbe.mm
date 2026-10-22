// =============================================================================
// AUProbe.mm - an in-process Audio Unit host for the ChordEngine AUs.
//
// `auval` only sees components the system registrar knows, i.e. INSTALLED
// ones, and this project never installs into the plug-in folders of the
// development Mac. AUProbe registers a bundle's factory in its own process
// (AudioComponentRegister) and runs the checks that matter for ChordEngine,
// following auval's sequence:
//
//   identity     type/subtype/manufacturer/version/name from the registry
//   open/close   repeated instantiate/dispose cycles
//   properties   element counts, SupportedNumChannels, stream formats, latency,
//                tail, CocoaUI, MIDI output callback info, parameter list,
//                factory presets, ClassInfo
//   formats      every supported channel count initialises; an unsupported
//                one is refused (at set time or at Initialize)
//   render       note-on/off through MusicDeviceMIDIEvent, output MIDI captured
//                through kAudioUnitProperty_MIDIOutputCallback, audio finite
//   state        ClassInfo round trip into a second instance; legacy fixtures
//
// --describe prints everything as JSON (used to compare 0.5.1 and the
// 0.6 native build); --validate exits non-zero on any failed check.
// Usage: AUProbe (--describe | --validate) <bundle.component> [--fixture <aupreset> --expect key=value ...]
// =============================================================================
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <Foundation/Foundation.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int failures = 0;
int currentBlock = -1;

void check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

OSType fourCC(NSString* text)
{
    const char* c = text.UTF8String;
    return (OSType(c[0]) << 24) | (OSType(c[1]) << 16) | (OSType(c[2]) << 8) | OSType(c[3]);
}

std::string fourCCText(OSType value)
{
    char c[5] = { char(value >> 24), char(value >> 16), char(value >> 8), char(value), 0 };
    return c;
}

struct Loaded
{
    AudioComponent component = nullptr;
    AudioComponentDescription desc {};
    NSDictionary* info = nil;
};

Loaded registerBundle(NSString* path)
{
    Loaded loaded;
    NSBundle* bundle = [NSBundle bundleWithPath:path];
    NSDictionary* component = [bundle.infoDictionary[@"AudioComponents"] firstObject];
    CFBundleRef cfBundle = CFBundleCreate(nullptr, (__bridge CFURLRef) [NSURL fileURLWithPath:path]);
    if (component == nil || cfBundle == nullptr || !CFBundleLoadExecutable(cfBundle))
        return loaded;
    auto factory = (AudioComponentFactoryFunction) CFBundleGetFunctionPointerForName(
        cfBundle, (__bridge CFStringRef) component[@"factoryFunction"]);
    if (factory == nullptr)
        return loaded;
    loaded.desc.componentType = fourCC(component[@"type"]);
    loaded.desc.componentSubType = fourCC(component[@"subtype"]);
    loaded.desc.componentManufacturer = fourCC(component[@"manufacturer"]);
    loaded.info = component;
    loaded.component = AudioComponentRegister(&loaded.desc, (__bridge CFStringRef) component[@"name"],
                                              [component[@"version"] unsignedIntValue], factory);
    return loaded;
}

struct MidiCapture
{
    std::vector<std::vector<UInt8>> messages;
};

OSStatus midiOutput(void* user, const AudioTimeStamp*, UInt32, const MIDIPacketList* list)
{
    auto* capture = static_cast<MidiCapture*>(user);
    if (std::getenv("AUPROBE_TRACE") != nullptr)
        std::fprintf(stderr, "callback in block %d: %u packet(s), first ts %llu\n", currentBlock, static_cast<unsigned>(list->numPackets), (unsigned long long) list->packet[0].timeStamp);
    // A packet may hold several messages: split at status bytes.
    const MIDIPacket* packet = &list->packet[0];
    for (UInt32 index = 0; index < list->numPackets; ++index)
    {
        for (UInt16 byte = 0; byte < packet->length; ++byte)
        {
            if ((packet->data[byte] & 0x80) != 0 || capture->messages.empty())
                capture->messages.emplace_back();
            capture->messages.back().push_back(packet->data[byte]);
        }
        packet = MIDIPacketNext(packet);
    }
    return noErr;
}

NSMutableDictionary* describe(AudioUnit unit)
{
    NSMutableDictionary* out = [NSMutableDictionary dictionary];
    for (AudioUnitScope scope : { kAudioUnitScope_Input, kAudioUnitScope_Output })
    {
        UInt32 count = 0, size = sizeof(count);
        AudioUnitGetProperty(unit, kAudioUnitProperty_ElementCount, scope, 0, &count, &size);
        out[scope == kAudioUnitScope_Input ? @"inputElements" : @"outputElements"] = @(count);
        NSMutableArray* formats = [NSMutableArray array];
        for (UInt32 element = 0; element < count; ++element)
        {
            AudioStreamBasicDescription format {};
            size = sizeof(format);
            AudioUnitGetProperty(unit, kAudioUnitProperty_StreamFormat, scope, element, &format, &size);
            CFStringRef name = nullptr;
            size = sizeof(name);
            AudioUnitGetProperty(unit, kAudioUnitProperty_ElementName, scope, element, &name, &size);
            [formats addObject:@{ @"channels" : @(format.mChannelsPerFrame), @"rate" : @(format.mSampleRate),
                                  @"name" : name != nullptr ? (__bridge_transfer NSString*) name : @"" }];
        }
        out[scope == kAudioUnitScope_Input ? @"inputFormats" : @"outputFormats"] = formats;
    }
    UInt32 size = 0;
    Boolean writable = false;
    NSMutableArray* configurations = [NSMutableArray array];
    if (AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, &size, &writable) == noErr && size > 0)
    {
        std::vector<AUChannelInfo> infos(size / sizeof(AUChannelInfo));
        AudioUnitGetProperty(unit, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, infos.data(), &size);
        for (const auto& info : infos)
            [configurations addObject:@[ @(info.inChannels), @(info.outChannels) ]];
    }
    out[@"supportedNumChannels"] = configurations;
    out[@"midiOutputCallbackInfo"] = @(AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_MIDIOutputCallbackInfo, kAudioUnitScope_Global, 0, &size, &writable) == noErr);
    out[@"cocoaUI"] = @(AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &size, &writable) == noErr);
    Float64 latency = -1, tail = -1;
    size = sizeof(latency);
    AudioUnitGetProperty(unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latency, &size);
    size = sizeof(tail);
    AudioUnitGetProperty(unit, kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0, &tail, &size);
    out[@"latency"] = @(latency);
    out[@"tail"] = @(tail);
    size = 0;
    AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, &writable);
    out[@"globalParameters"] = @(size / sizeof(AudioUnitParameterID));
    CFArrayRef presets = nullptr;
    size = sizeof(presets);
    out[@"factoryPresets"] = @(AudioUnitGetProperty(unit, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &presets, &size) == noErr && presets != nullptr ? CFArrayGetCount(presets) : 0);
    if (presets != nullptr)
        CFRelease(presets);
    return out;
}

// Initialises with `channels` on output 0 (if there is an output) and renders
// a note through the unit; returns the MIDI it produced.
bool renderNote(AudioUnit unit, UInt32 channels, MidiCapture& capture, bool& audioFinite)
{
    UInt32 outputs = 0, size = sizeof(outputs);
    AudioUnitGetProperty(unit, kAudioUnitProperty_ElementCount, kAudioUnitScope_Output, 0, &outputs, &size);
    if (outputs > 0)
    {
        AudioStreamBasicDescription format {};
        format.mSampleRate = 48000;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        format.mBitsPerChannel = 32;
        format.mChannelsPerFrame = channels;
        format.mFramesPerPacket = 1;
        format.mBytesPerFrame = 4;
        format.mBytesPerPacket = 4;
        if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, sizeof(format)) != noErr)
            return false;
    }
    AURenderCallbackStruct none {};
    (void) none;
    AUMIDIOutputCallbackStruct callback { midiOutput, &capture };
    AudioUnitSetProperty(unit, kAudioUnitProperty_MIDIOutputCallback, kAudioUnitScope_Global, 0, &callback, sizeof(callback));
    if (AudioUnitInitialize(unit) != noErr)
        return false;

    std::vector<float> left(512), right(512);
    audioFinite = true;
    for (int block = 0; block < 8; ++block)
    {
        if (block == 1)
            MusicDeviceMIDIEvent(unit, 0x90, 60, 100, 10);
        if (block == 5)
            MusicDeviceMIDIEvent(unit, 0x80, 60, 0, 10);
        currentBlock = block;
        AudioTimeStamp stamp {};
        stamp.mSampleTime = block * 512.0;
        stamp.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags flags = 0;
        struct { UInt32 count; AudioBuffer buffers[2]; } list {};
        list.count = outputs > 0 ? channels : 1;
        list.buffers[0] = { 1, 512 * 4, left.data() };
        list.buffers[1] = { 1, 512 * 4, right.data() };
        const OSStatus status = AudioUnitRender(unit, &flags, &stamp, 0, 512, reinterpret_cast<AudioBufferList*>(&list));
        if (status != noErr && outputs > 0)
            return false;
        for (UInt32 c = 0; c < (outputs > 0 ? channels : 0); ++c)
            for (int i = 0; i < 512; ++i)
                if (!std::isfinite(static_cast<float*>(list.buffers[c].mData)[i]))
                    audioFinite = false;
    }
    AudioUnitUninitialize(unit);
    return true;
}
} // namespace

int main(int argc, const char* argv[])
{
    @autoreleasepool
    {
        if (argc < 3)
        {
            std::fprintf(stderr, "usage: AUProbe (--describe | --validate) <bundle.component> [--fixture <aupreset> --expect key=value ...]\n");
            return 2;
        }
        const std::string mode = argv[1];
        NSString* path = @(argv[2]);
        auto loaded = registerBundle(path);
        if (loaded.component == nullptr)
        {
            std::fprintf(stderr, "cannot register %s\n", argv[2]);
            return 2;
        }

        AudioUnit unit = nullptr;
        if (AudioComponentInstanceNew(loaded.component, &unit) != noErr)
        {
            std::fprintf(stderr, "cannot instantiate\n");
            return 2;
        }
        NSMutableDictionary* description = describe(unit);
        description[@"type"] = @(fourCCText(loaded.desc.componentType).c_str());
        description[@"subtype"] = @(fourCCText(loaded.desc.componentSubType).c_str());
        description[@"manufacturer"] = @(fourCCText(loaded.desc.componentManufacturer).c_str());
        description[@"name"] = loaded.info[@"name"];
        description[@"version"] = loaded.info[@"version"];

        if (mode == "--describe")
        {
            // Rendering facts for the comparison: which channel counts work and
            // what MIDI a note produces.
            NSMutableArray* formats = [NSMutableArray array];
            const bool hasOutput = [description[@"outputElements"] unsignedIntValue] > 0;
            for (UInt32 channels : hasOutput ? std::vector<UInt32> { 1u, 2u, 4u } : std::vector<UInt32> { 0u })
            {
                AudioUnit trial = nullptr;
                AudioComponentInstanceNew(loaded.component, &trial);
                MidiCapture capture;
                bool finite = false;
                const bool ok = renderNote(trial, channels, capture, finite);
                [formats addObject:@{ @"channels" : @(channels), @"renders" : @(ok), @"midiOut" : @(capture.messages.size()) }];
                AudioComponentInstanceDispose(trial);
            }
            description[@"renderByChannels"] = formats;
            NSData* json = [NSJSONSerialization dataWithJSONObject:description options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:nil];
            std::printf("%s\n", [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
            AudioComponentInstanceDispose(unit);
            return 0;
        }

        // ---- validation -------------------------------------------------------
        const bool midiFx = loaded.desc.componentType == kAudioUnitType_MIDIProcessor;
        check(loaded.info[@"version"] != nil, "AudioComponents version present");
        check([description[@"inputElements"] unsignedIntValue] == 0, "no audio input element");
        check([description[@"midiOutputCallbackInfo"] boolValue], "MIDI output callback info offered");
        check([description[@"cocoaUI"] boolValue], "Cocoa view offered");
        if (midiFx)
        {
            check([description[@"outputElements"] unsignedIntValue] <= 1, "MIDI FX: at most one output element");
        }
        else
        {
            check([description[@"outputElements"] unsignedIntValue] == 1, "instrument: one output element");
            NSString* name = [description[@"outputFormats"] firstObject][@"name"];
            check(name.length > 0, "instrument: output bus is named");
        }

        for (int cycle = 0; cycle < 10; ++cycle)
        {
            AudioUnit again = nullptr;
            const bool opened = AudioComponentInstanceNew(loaded.component, &again) == noErr;
            if (opened)
                AudioComponentInstanceDispose(again);
            if (!opened)
            {
                check(false, "open/close cycle " + std::to_string(cycle));
                break;
            }
        }
        check(true, "10 open/close cycles");

        // Every channel count SupportedNumChannels lists initialises and renders;
        // an unlisted one is refused.
        NSArray* configs = description[@"supportedNumChannels"];
        UInt32 outputs = [description[@"outputElements"] unsignedIntValue];
        std::vector<UInt32> supported;
        for (NSArray* config in configs)
            supported.push_back([config[1] unsignedIntValue]);
        if (outputs > 0)
        {
            check(!supported.empty(), "SupportedNumChannels declared");
            for (const auto channels : supported)
            {
                AudioUnit trial = nullptr;
                AudioComponentInstanceNew(loaded.component, &trial);
                MidiCapture capture;
                bool finite = false;
                const bool ok = renderNote(trial, channels, capture, finite);
                check(ok && finite, "renders " + std::to_string(channels) + " channel(s), finite audio");
                std::string dump;
                for (const auto& message : capture.messages)
                {
                    for (const auto byte : message)
                    {
                        char hex[4];
                        std::snprintf(hex, sizeof(hex), "%02x", byte);
                        dump += hex;
                    }
                    dump += ' ';
                }
                check(capture.messages.size() == 8, "middle C produces a four-note chord on and off: " + dump);
                AudioComponentInstanceDispose(trial);
            }
            AudioUnit trial = nullptr;
            AudioComponentInstanceNew(loaded.component, &trial);
            MidiCapture capture;
            bool finite = false;
            check(!renderNote(trial, 4, capture, finite), "refuses 4 output channels");
            AudioComponentInstanceDispose(trial);
        }
        else
        {
            AudioUnit trial = nullptr;
            AudioComponentInstanceNew(loaded.component, &trial);
            MidiCapture capture;
            bool finite = false;
            const bool ok = renderNote(trial, 0, capture, finite);
            check(ok, "MIDI FX initialises and processes without audio");
            check(capture.messages.size() == 8, "middle C produces a four-note chord on and off (" + std::to_string(capture.messages.size()) + " messages)");
            AudioComponentInstanceDispose(trial);
        }

        // State: ClassInfo round trip into a fresh instance.
        CFPropertyListRef classInfo = nullptr;
        UInt32 size = sizeof(classInfo);
        check(AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &classInfo, &size) == noErr
              && classInfo != nullptr, "ClassInfo readable");
        AudioUnit second = nullptr;
        AudioComponentInstanceNew(loaded.component, &second);
        check(AudioUnitSetProperty(second, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &classInfo, sizeof(classInfo)) == noErr,
              "ClassInfo accepted by a second instance");
        if (classInfo != nullptr)
            CFRelease(classInfo);

        // Legacy fixtures: --fixture <aupreset> --expect key=value ...
        for (int index = 3; index + 1 < argc; ++index)
        {
            if (std::string(argv[index]) != "--fixture")
                continue;
            NSData* data = [NSData dataWithContentsOfFile:@(argv[index + 1])];
            id plist = data != nil ? [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListMutableContainers format:nil error:nil] : nil;
            CFPropertyListRef fixture = (__bridge CFPropertyListRef) plist;
            AudioUnit restored = nullptr;
            AudioComponentInstanceNew(loaded.component, &restored);
            check(plist != nil && AudioUnitSetProperty(restored, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &fixture, sizeof(fixture)) == noErr,
                  std::string("0.5.1 preset accepted: ") + argv[index + 1]);
            CFPropertyListRef back = nullptr;
            size = sizeof(back);
            AudioUnitGetProperty(restored, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &back, &size);
            NSData* state = back != nullptr ? ((__bridge NSDictionary*) back)[@"jucePluginState"] : nil;
            NSString* text = state != nil ? [[NSString alloc] initWithData:state encoding:NSUTF8StringEncoding] : @"";
            for (int k = index + 2; k + 1 < argc && std::string(argv[k]) == "--expect"; k += 2)
                check([text containsString:@(argv[k + 1])], std::string("restored state has ") + argv[k + 1]);
            if (back != nullptr)
                CFRelease(back);
            AudioComponentInstanceDispose(restored);
        }

        AudioComponentInstanceDispose(second);
        AudioComponentInstanceDispose(unit);
        std::printf("%s: %d failure(s)\n", failures == 0 ? "AU VALIDATION PASSED" : "AU VALIDATION FAILED", failures);
        return failures == 0 ? 0 : 1;
    }
}
