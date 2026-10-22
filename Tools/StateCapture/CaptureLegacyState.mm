// =============================================================================
// CaptureLegacyState.mm - records the host state a SHIPPED ChordEngine build
// writes, as test fixtures for the JUCE-free build (Tests/Fixtures/State051).
//
// It is a host-shaped tool, not part of the product:
//   * AU: the bundle's factory is registered IN THIS PROCESS only
//     (AudioComponentRegister), so nothing is installed. The unit's ClassInfo
//     is read (default state), then set to a dictionary whose "jucePluginState"
//     carries a non-default configuration, and read back: the bytes the
//     shipped plug-in itself re-serialised.
//   * VST3: the module is loaded with the VST 3 SDK's hosting helpers; the
//     component's getState is captured before and after setState with the same
//     non-default configuration.
//
// Usage: CaptureLegacyState <out dir> <MIDI FX .component> <Instrument .component> <.vst3>
// =============================================================================
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"

#include <cstdio>
#include <string>

using namespace Steinberg;

namespace
{
// A non-default configuration touching every field 0.5.1 persists, including
// a pack preset whose name needs XML character references.
std::string customState()
{
    std::string map;
    for (int index = 0; index < 73; ++index)
    {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", (index * 11) % 70);
        map += hex;
    }
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n"
           "<CHORDENGINE_STATE version=\"1\" keyIndex=\"3\" scale=\"dorian\" chordPreset=\"neoSoul\" "
           "transposeTarget=\"lowest\" transposeOctave=\"-1\" velocityMode=\"fixed\" fixedVelocity=\"77\" "
           "packSlug=\"chordengine-test-pack\" packName=\"Test &amp; Pack\" packPreset=\"Caf&#233; &#9839;\" "
           "packVoicing=\"open\" packMap=\"" + map + "\" internalAudio=\"on\" internalSound=\"bass\" "
           "recordQuantize=\"thirdBeat\" followHostRecord=\"on\"/>\n";
}

bool writeData(NSString* path, NSData* data)
{
    return [data writeToFile:path atomically:YES];
}

OSType fourCC(NSString* text)
{
    const char* c = text.UTF8String;
    return (OSType(c[0]) << 24) | (OSType(c[1]) << 16) | (OSType(c[2]) << 8) | OSType(c[3]);
}

int captureAU(NSString* outDir, NSString* name, NSString* bundlePath)
{
    NSBundle* bundle = [NSBundle bundleWithPath:bundlePath];
    NSDictionary* component = [bundle.infoDictionary[@"AudioComponents"] firstObject];
    CFBundleRef cfBundle = CFBundleCreate(nullptr, (__bridge CFURLRef) [NSURL fileURLWithPath:bundlePath]);
    if (bundle == nil || component == nil || cfBundle == nullptr || !CFBundleLoadExecutable(cfBundle))
    {
        std::fprintf(stderr, "cannot load %s\n", bundlePath.UTF8String);
        return 1;
    }
    auto factory = (AudioComponentFactoryFunction) CFBundleGetFunctionPointerForName(
        cfBundle, (__bridge CFStringRef) component[@"factoryFunction"]);
    if (factory == nullptr)
    {
        std::fprintf(stderr, "no factory in %s\n", bundlePath.UTF8String);
        return 1;
    }
    AudioComponentDescription desc {};
    desc.componentType = fourCC(component[@"type"]);
    desc.componentSubType = fourCC(component[@"subtype"]);
    desc.componentManufacturer = fourCC(component[@"manufacturer"]);
    AudioComponent registered = AudioComponentRegister(&desc, (__bridge CFStringRef) component[@"name"],
                                                       [component[@"version"] unsignedIntValue], factory);
    AudioUnit unit = nullptr;
    if (registered == nullptr || AudioComponentInstanceNew(registered, &unit) != noErr)
    {
        std::fprintf(stderr, "cannot instantiate %s\n", bundlePath.UTF8String);
        return 1;
    }

    CFPropertyListRef classInfo = nullptr;
    UInt32 size = sizeof(classInfo);
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &classInfo, &size) != noErr)
        return 1;
    NSData* defaultPlist = [NSPropertyListSerialization dataWithPropertyList:(__bridge id) classInfo
                                                                      format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
    writeData([outDir stringByAppendingFormat:@"/%@-default.aupreset", name], defaultPlist);

    NSMutableDictionary* custom = [(__bridge NSDictionary*) classInfo mutableCopy];
    CFRelease(classInfo);
    const auto state = customState();
    custom[@"jucePluginState"] = [NSData dataWithBytes:state.data() length:state.size()];
    CFPropertyListRef customRef = (__bridge CFPropertyListRef) custom;
    if (AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &customRef, sizeof(customRef)) != noErr)
        return 1;
    classInfo = nullptr;
    size = sizeof(classInfo);
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &classInfo, &size) != noErr)
        return 1;
    NSData* customPlist = [NSPropertyListSerialization dataWithPropertyList:(__bridge id) classInfo
                                                                     format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
    writeData([outDir stringByAppendingFormat:@"/%@-custom.aupreset", name], customPlist);
    NSData* stateBytes = ((__bridge NSDictionary*) classInfo)[@"jucePluginState"];
    writeData([outDir stringByAppendingFormat:@"/%@-custom.state", name], stateBytes);
    CFRelease(classInfo);
    AudioComponentInstanceDispose(unit);
    std::printf("AU %s: captured (%lu state bytes)\n", name.UTF8String, (unsigned long) stateBytes.length);
    return 0;
}

int captureVST3(NSString* outDir, NSString* path)
{
    std::string error;
    auto module = VST3::Hosting::Module::create(path.UTF8String, error);
    if (!module)
    {
        std::fprintf(stderr, "cannot load VST3: %s\n", error.c_str());
        return 1;
    }
    auto factory = module->getFactory();
    Vst::HostApplication host;
    for (const auto& info : factory.classInfos())
    {
        if (info.category() != kVstAudioEffectClass)
            continue;
        auto component = factory.createInstance<Vst::IComponent>(info.ID());
        if (!component || component->initialize(&host) != kResultOk)
            return 1;
        auto save = [&](const char* suffix)
        {
            MemoryStream stream;
            component->getState(&stream);
            NSData* data = [NSData dataWithBytes:stream.getData() length:(NSUInteger) stream.getSize()];
            writeData([outDir stringByAppendingFormat:@"/vst3-%s.state", suffix], data);
            return data.length;
        };
        const auto defaultSize = save("default");
        const auto state = customState();
        MemoryStream input(const_cast<char*>(state.data()), static_cast<TSize>(state.size()));
        component->setState(&input);
        const auto customSize = save("custom");
        component->terminate();
        std::printf("VST3 %s: captured (%lu / %lu bytes), class %s\n", info.name().c_str(),
                    (unsigned long) defaultSize, (unsigned long) customSize, info.ID().toString().c_str());
        return 0;
    }
    return 1;
}
} // namespace

int main(int argc, const char* argv[])
{
    @autoreleasepool
    {
        if (argc != 5)
        {
            std::fprintf(stderr, "usage: CaptureLegacyState <out dir> <MIDI FX .component> <Instrument .component> <.vst3>\n");
            return 2;
        }
        NSString* outDir = @(argv[1]);
        [[NSFileManager defaultManager] createDirectoryAtPath:outDir withIntermediateDirectories:YES attributes:nil error:nil];
        int failures = 0;
        failures += captureAU(outDir, @"au-midifx", @(argv[2]));
        failures += captureAU(outDir, @"au-instrument", @(argv[3]));
        failures += captureVST3(outDir, @(argv[4]));
        return failures == 0 ? 0 : 1;
    }
}
