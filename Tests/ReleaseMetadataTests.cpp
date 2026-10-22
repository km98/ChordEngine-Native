/*
  ChordEngine release-metadata gate.

  Classification: AUTOMATED, HEAD-LESS release-candidate check. It is NOT a DAW
  test and NOT host acceptance sign-off. Nothing here installs a plug-in, opens
  a window or drives another application.

  What it proves
    * the AU and VST3 bundles THIS BUILD just produced (never an installed
      copy) carry the frozen product identity, the expected version, the
      expected VST3 class id and the expected `Instrument` category;
    * the metadata a host reads declares MIDI input/output; the MIDI FX AU is
      a MIDI effect with NO audio bus, the VST3 has one stereo output for the
      internal synth, and that output stays silent for a trigger while
      internal audio is off (its default);
    * the ChordEngine Instrument AU carries its own frozen identity
      (aumu / Cein / Musc, com.musicprod.chordengine.instrument);
    * the compiled product version reported by the plug-in, by the licensing
      `plugin_version` telemetry and by the update client is the same value;
    * no placeholder identifier (`Abcd`, `com.myCompany.product`), no
      development label and no reference-product version (`0.4.0`) survives in
      the shipped metadata;
    * both bundles carry a complete bundle signature that passes
      `codesign --verify --deep --strict`, with Info.plist bound and resources
      sealed - so a resource written after signing (the VST3's
      moduleinfo.json) or a linker-only signature (the AU) fails the gate.

  What it explicitly does NOT prove
    * that FL Studio exposes an event/MIDI input for an `Instrument`-category
      VST3 with no audio bus, or that it classifies the plug-in as a Generator.
      Only a human FL Studio session can show that;
    * that Logic Pro loads the AU, shows the editor, restores state or lists the
      plug-in once. `Docs/LOGIC_HOST_TEST_CHECKLIST.md` covers that by hand;
    * anything about Developer ID signed, notarized or installed artefacts
      (the build signs ad hoc; Tools/PackageMacDMG.sh does the release signing).

  Usage:
      ChordEngineReleaseMetadataTests [au-bundle] [vst3-bundle] [instrument-au-bundle]

  With no arguments the bundles are taken from CHORDENGINE_AU_BUNDLE /
  CHORDENGINE_VST3_BUNDLE if set, otherwise the newest ChordEngine.component /
  ChordEngine.vst3 under CHORDENGINE_BUILD_DIR (the build tree this test was
  configured from) is used, so the check runs on a fresh build without ever
  touching an installed plug-in.

  Exit codes: 0 = every check passed, 1 = a check failed,
              77 = no built bundle was found (CTest reports "skipped", which is
              never a pass).
*/

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include "Licensing/LicensingTypes.h"
#include "Updates/UpdateTypes.h"

#include <cstdio>
#include <vector>

namespace
{
constexpr const char* expectedAuBundleId = "com.musicprod.chordengine";
constexpr const char* expectedAuType = "aumi";
constexpr const char* expectedAuManufacturer = "Musc";
constexpr const char* expectedAuSubtype = "Cefl";
constexpr const char* expectedAuDescription = "Music-Prod: ChordEngine";

// The second AU (an instrument, so Logic Pro can play the internal synth).
constexpr const char* expectedInstrumentBundleId = "com.musicprod.chordengine.instrument";
constexpr const char* expectedInstrumentType = "aumu";
constexpr const char* expectedInstrumentSubtype = "Cein";
constexpr const char* expectedInstrumentName = "ChordEngine Instrument";
constexpr const char* expectedInstrumentDescription = "Music-Prod: ChordEngine Instrument";

struct AuIdentity
{
    const char* label;
    const char* bundleId;
    const char* bundleName;
    const char* type;
    const char* subtype;
    const char* description;
};

constexpr AuIdentity midiEffectAu { "AU", expectedAuBundleId, "ChordEngine", expectedAuType,
                                    expectedAuSubtype, expectedAuDescription };
constexpr AuIdentity instrumentAu { "Instrument AU", expectedInstrumentBundleId,
                                    expectedInstrumentName, expectedInstrumentType,
                                    expectedInstrumentSubtype, expectedInstrumentDescription };

constexpr const char* expectedVst3BundleId = "com.musicprod.chordenginefl";

// JUCE derives the VST3 class id from the manufacturer code and the plug-in
// code: ABCDEF01 + the fixed JUCE salt + hex("Musc") + hex("Cefl"). This is the
// legacy ChordEngine FL identity, so every existing FL Studio project recorded
// exactly this id.
constexpr const char* expectedVst3ClassId = "ABCDEF019182FAEB4D7573634365666C";
constexpr const char* expectedVst3Category = "Instrument";

constexpr const char* expectedProductSlug = "chordengine";
constexpr const char* expectedVendor = "Music-Prod";
constexpr const char* expectedPluginName = "ChordEngine";

int checksRun = 0;
int failures = 0;

juce::String expectedVersion() { return juce::String(CHORDENGINE_EXPECTED_VERSION); }

void note(const juce::String& text) { std::printf("      %s\n", text.toRawUTF8()); }

void pass(const juce::String& what)
{
    ++checksRun;
    std::printf("PASS  %s\n", what.toRawUTF8());
}

void fail(const juce::String& what)
{
    ++checksRun;
    ++failures;
    std::printf("FAIL  %s\n", what.toRawUTF8());
}

void require(bool condition, const juce::String& what)
{
    if (condition)
        pass(what);
    else
        fail(what);
}

void expectEqual(const juce::String& actual, const juce::String& expected, const juce::String& what)
{
    require(actual == expected,
            what + " (expected \"" + expected + "\", got \"" + actual + "\")");
}

// ---------------------------------------------------------------- plist reads
// Info.plist is XML for both formats, so it is read as a plist tree rather than
// with a JSON parser.
const juce::XmlElement* valueFor(const juce::XmlElement& dict, const juce::String& key)
{
    for (int i = 0; i < dict.getNumChildElements(); ++i)
    {
        auto* element = dict.getChildElement(i);

        if (element->hasTagName("key") && element->getAllSubText().trim() == key)
            return dict.getChildElement(i + 1); // the value element follows its key
    }

    return nullptr;
}

juce::String plistText(const juce::XmlElement& dict, const juce::String& key)
{
    if (auto* value = valueFor(dict, key))
        return value->getAllSubText().trim();

    return {};
}

const juce::XmlElement* firstChildDictNamed(const juce::XmlElement& parent, const juce::String& tag)
{
    for (auto* child : parent.getChildIterator())
        if (child->hasTagName(tag))
            return child;

    return nullptr;
}

// ------------------------------------------------------- moduleinfo.json reads
// JUCE writes moduleinfo.json with trailing commas, so it is not strict JSON and
// is deliberately NOT parsed with a JSON parser. These helpers pull one scalar
// or one array block out of the text, the same way the packaging pipeline does.
// `true` when the module metadata declares EXACTLY this key/value pair. The
// value is asserted textually for the same reason the packaging pipeline reads
// it textually: JUCE writes moduleinfo.json with trailing commas, so no strict
// JSON parser accepts the file.
bool moduleMetadataDeclares(const juce::String& moduleInfo, const juce::String& key,
                            const juce::String& value)
{
    return moduleInfo.contains("\"" + key + "\": \"" + value + "\"");
}

int countOccurrences(const juce::String& text, const juce::String& needle)
{
    int count = 0;

    for (int index = text.indexOf(needle); index >= 0;
         index = text.indexOf(index + needle.length(), needle))
        ++count;

    return count;
}

juce::String jsonArrayBlock(const juce::String& text, const juce::String& key)
{
    const auto pattern = "\"" + key + "\":";
    const auto index = text.indexOf(pattern);

    if (index < 0)
        return {};

    const auto rest = text.substring(index + pattern.length());
    const auto open = rest.indexOfChar('[');
    const auto close = rest.indexOfChar(']');

    if (open < 0 || close < open)
        return {};

    return rest.substring(open, close + 1);
}

// ------------------------------------------------------------- placeholders
void scanForStaleMetadata(const juce::String& label, const juce::String& text)
{
    for (const auto* placeholder : { "Abcd", "com.myCompany.product" })
        require(! text.contains(placeholder),
                label + " no longer contains the placeholder identifier \"" + placeholder + "\"");

    require(! text.contains("0.4.0"),
            label + " does not carry the reference product's version 0.4.0");
    require(! text.contains("development build"),
            label + " is not labelled a development build");
}

// ------------------------------------------------------------------ AU checks
void checkAuMetadata(const juce::File& bundle, const AuIdentity& identity)
{
    std::printf("\n--- %s metadata: %s\n", identity.label, bundle.getFullPathName().toRawUTF8());
    const juce::String label(identity.label);

    const auto plistFile = bundle.getChildFile("Contents/Info.plist");

    if (! plistFile.existsAsFile())
    {
        fail(label + " bundle has a Contents/Info.plist");
        return;
    }

    const auto rawText = plistFile.loadFileAsString();
    scanForStaleMetadata(label + " Info.plist", rawText);

    auto document = juce::XmlDocument::parse(plistFile);

    if (document == nullptr)
    {
        fail(label + " Info.plist parses as a plist document");
        return;
    }

    const auto* dict = firstChildDictNamed(*document, "dict");

    if (dict == nullptr)
    {
        fail(label + " Info.plist has a root dictionary");
        return;
    }

    expectEqual(plistText(*dict, "CFBundleIdentifier"), identity.bundleId,
                label + " bundle identifier is the product's");
    expectEqual(plistText(*dict, "CFBundleShortVersionString"), expectedVersion(),
                label + " short version is the release version");
    expectEqual(plistText(*dict, "CFBundleName"), identity.bundleName,
                label + " bundle name is " + identity.bundleName);

    // The AU identity a host resolves a saved project by: the component triple.
    const auto* components = valueFor(*dict, "AudioComponents");

    if (components == nullptr)
    {
        fail(label + " Info.plist declares AudioComponents");
        return;
    }

    require(components->getNumChildElements() == 1,
            label + " declares exactly one AudioComponent (a second one would be a second "
            "registration of the same product)");

    const auto* component = components->getChildElement(0);

    if (component == nullptr || ! component->hasTagName("dict"))
    {
        fail(label + " AudioComponents entry is a dictionary");
        return;
    }

    expectEqual(plistText(*component, "type"), identity.type,
                label + " component type is " + identity.type);
    expectEqual(plistText(*component, "manufacturer"), expectedAuManufacturer,
                label + " manufacturer code is Musc");
    expectEqual(plistText(*component, "subtype"), identity.subtype,
                label + " subtype code is " + identity.subtype);
    expectEqual(plistText(*component, "name"), identity.description,
                label + " displays the product and manufacturer names");

    require(plistText(*component, "factoryFunction").isNotEmpty(),
            label + " declares its factory function");

    const auto version = plistText(*component, "version");
    require(version.isNotEmpty() && version != "0",
            label + " component version is set (got \"" + version + "\")");

    note(label + " " + plistText(*dict, "CFBundleIdentifier") + "  v"
         + plistText(*dict, "CFBundleShortVersionString") + "  "
         + plistText(*component, "type") + " / " + plistText(*component, "manufacturer")
         + " / " + plistText(*component, "subtype"));
}

// ---------------------------------------------------------------- VST3 checks
void checkVst3Metadata(const juce::File& bundle)
{
    std::printf("\n--- VST3 metadata: %s\n", bundle.getFullPathName().toRawUTF8());

    const auto plistFile = bundle.getChildFile("Contents/Info.plist");
    const auto moduleInfoFile = bundle.getChildFile("Contents/Resources/moduleinfo.json");

    if (! plistFile.existsAsFile() || ! moduleInfoFile.existsAsFile())
    {
        fail("VST3 bundle has Contents/Info.plist and Contents/Resources/moduleinfo.json");
        return;
    }

    const auto plistText_ = plistFile.loadFileAsString();
    scanForStaleMetadata("VST3 Info.plist", plistText_);

    if (auto document = juce::XmlDocument::parse(plistFile))
    {
        if (const auto* dict = firstChildDictNamed(*document, "dict"))
        {
            expectEqual(plistText(*dict, "CFBundleIdentifier"), expectedVst3BundleId,
                        "VST3 bundle identifier is the legacy-compatible one");
            expectEqual(plistText(*dict, "CFBundleShortVersionString"), expectedVersion(),
                        "VST3 short version is the release version");
        }
        else
        {
            fail("VST3 Info.plist has a root dictionary");
        }
    }
    else
    {
        fail("VST3 Info.plist parses as a plist document");
    }

    const auto moduleInfo = moduleInfoFile.loadFileAsString();
    scanForStaleMetadata("VST3 moduleinfo.json", moduleInfo);

    require(moduleMetadataDeclares(moduleInfo, "Name", expectedPluginName),
            "VST3 module metadata declares the product name ChordEngine");
    require(moduleMetadataDeclares(moduleInfo, "Version", expectedVersion()),
            "VST3 module metadata declares the release version " + expectedVersion());
    require(moduleMetadataDeclares(moduleInfo, "Vendor", expectedVendor),
            "VST3 module metadata declares the Music-Prod vendor");

    // Exactly one audio module class, carrying the frozen class id.
    require(1 == countOccurrences(moduleInfo, "Audio Module Class"),
            "VST3 declares exactly one audio module class (the plug-in itself)");

    require(moduleInfo.contains(expectedVst3ClassId),
            "VST3 class id is the legacy-compatible " + juce::String(expectedVst3ClassId)
                + " (derived from the Musc/Cefl code pair)");

    const auto subCategories = jsonArrayBlock(moduleInfo, "Sub Categories");
    require(subCategories.isNotEmpty(), "VST3 declares Sub Categories");
    require(subCategories.contains(expectedVst3Category),
            "VST3 category is " + juce::String(expectedVst3Category)
                + " (an Fx module gets no event input in FL Studio): " + subCategories);
    require(! subCategories.contains("Fx"),
            "VST3 category is no longer Fx: " + subCategories);

    note("VST3 " + juce::String(expectedVst3BundleId) + "  v" + expectedVersion()
         + "  CID " + juce::String(expectedVst3ClassId) + "  " + subCategories);
}

// ------------------------------------------------------------ bundle signature
// The build ad hoc signs each finished bundle (Tools/AdhocSignBundle.cmake).
// Strict verification is what an Apple Silicon host and the release packaging
// rely on: it fails when Info.plist is not bound, when the signature seals no
// resources, and when a file was added to the bundle after signing.
bool runCodesign(const juce::StringArray& arguments, juce::String& output)
{
    juce::ChildProcess process;
    juce::StringArray command { "/usr/bin/codesign" };
    command.addArray(arguments);

    if (! process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return false;

    output = process.readAllProcessOutput();
    process.waitForProcessToFinish(30000);
    return process.getExitCode() == 0;
}

void checkBundleSignature(const juce::File& bundle, const juce::String& label)
{
    std::printf("\n--- %s signature: %s\n", label.toRawUTF8(), bundle.getFullPathName().toRawUTF8());

    juce::String verifyOutput;
    const bool verified = runCodesign({ "--verify", "--deep", "--strict", bundle.getFullPathName() },
                                      verifyOutput);
    require(verified, label + " bundle passes codesign --verify --deep --strict");

    if (! verified)
        note(verifyOutput.trim());

    juce::String details;
    runCodesign({ "-dvvv", bundle.getFullPathName() }, details);

    require(details.contains("Info.plist entries="),
            label + " signature binds Info.plist");
    require(details.contains("Sealed Resources version="),
            label + " signature seals the bundle resources");
}

// ------------------------------------------------------- host-derived checks
// Loads the BUILT bundle through JUCE's head-less hosting - the same wrapper a
// host loads - and records the bus/event layout it derives. This is where an
// "Instrument with MIDI in/out and no audio bus" is actually visible.
void checkHostedBundle(juce::AudioPluginFormatManager& formats,
                       const juce::File& bundle,
                       const juce::String& label,
                       bool required,
                       bool expectsMidiEffectFlag,
                       int expectedOutputBuses)
{
    std::printf("\n--- hosted %s: %s\n", label.toRawUTF8(), bundle.getFullPathName().toRawUTF8());

    auto* format = [&]() -> juce::AudioPluginFormat*
    {
        for (int i = 0; i < formats.getNumFormats(); ++i)
            if (auto* candidate = formats.getFormat(i);
                candidate->fileMightContainThisPluginType(bundle.getFullPathName()))
                return candidate;

        return nullptr;
    }();

    if (format == nullptr)
    {
        // A build-tree AU is not registered with the system, so the AU host
        // format may refuse it here. That is reported as an explicit skip, never
        // as a pass: the AU's identity is still checked from its plist above and
        // the AU host test stays manual.
        if (required)
            fail(label + ": an installed host format claims the built bundle");
        else
            note("SKIP  no host format claims this bundle (AU host testing stays manual)");

        return;
    }

    juce::OwnedArray<juce::PluginDescription> descriptions;
    format->findAllTypesForFile(descriptions, bundle.getFullPathName());

    if (descriptions.isEmpty())
    {
        if (required)
            fail(label + ": the " + format->getName() + " host reads a plugin class from the built bundle");
        else
            note("SKIP  the " + format->getName() + " host cannot read this bundle (AU host testing "
                 "stays manual)");

        return;
    }

    require(descriptions.size() == 1,
            label + " declares exactly one plugin class");

    for (auto* description : descriptions)
    {
        expectEqual(description->name, expectedPluginName, label + " class name is ChordEngine");
        expectEqual(description->manufacturerName, expectedVendor, label + " manufacturer is Music-Prod");
        expectEqual(description->version, expectedVersion(), label + " class version is the release version");

        juce::String error;
        auto instance = formats.createPluginInstance(*description, 44100.0, 512, error);

        if (instance == nullptr)
        {
            fail(label + " instantiates through the host format: " + error);
            continue;
        }

        auto* processor = instance.get();

        const int audioInputBuses = processor->getBusCount(true);
        const int audioOutputBuses = processor->getBusCount(false);

        note("buses in " + juce::String(audioInputBuses) + " / out " + juce::String(audioOutputBuses)
             + "   channels in " + juce::String(processor->getTotalNumInputChannels())
             + " / out " + juce::String(processor->getTotalNumOutputChannels()));

        require(processor->acceptsMidi(), label + " declares MIDI input to the host");
        require(processor->producesMidi(),
                label + " declares MIDI output to the host (without it no host can route "
                        "the generated chord)");
        // `isMidiEffect()` is an AU concept: JUCE derives it from the component
        // type being kAudioUnitType_MIDIProcessor, which is what `aumi` means.
        // VST3 has no equivalent flag - the hosted VST3 instance reports no even
        // for this plug-in - so there the MIDI-processor nature is the event
        // ports plus the absence of any audio bus, which is what the checks
        // below assert. Asserting the AU flag here is the point of the AU row.
        if (expectsMidiEffectFlag)
            require(processor->isMidiEffect(), label + " is declared a MIDI effect (aumi)");
        else
            note("isMidiEffect: " + juce::String(processor->isMidiEffect() ? "yes" : "no")
                 + " (VST3 carries no MIDI-effect flag; the AU carries it via aumi)");
        require(audioInputBuses == 0 && audioOutputBuses == expectedOutputBuses,
                label + " exposes " + juce::String(expectedOutputBuses)
                    + " audio output bus(es) and no input (in "
                    + juce::String(audioInputBuses) + " / out " + juce::String(audioOutputBuses) + ")");
        if (expectedOutputBuses > 0)
            require(processor->getChannelCountOfBus(false, 0) == 2,
                    label + " internal-audio output is stereo");

        // With internal audio off (the MIDI FX and VST3 default) a trigger
        // must emit no audio. The generated chord itself is asserted by
        // ChordEngineHostBoundaryProbe; here the point is that the audio path
        // stays silent.
        processor->prepareToPlay(44100.0, 512);

        juce::MidiBuffer events;
        events.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 96), 0);

        juce::AudioBuffer<float> audio(juce::jmax(1, processor->getTotalNumOutputChannels()), 512);
        audio.clear();
        processor->processBlock(audio, events);

        auto peak = 0.0f;

        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            peak = juce::jmax(peak, audio.getMagnitude(channel, 0, audio.getNumSamples()));

        require(peak == 0.0f, label + " produces no audio for a trigger (peak " + juce::String(peak) + ")");

        processor->releaseResources();
    }
}

// ------------------------------------------------------------- constants
void checkCompiledConstants()
{
    std::printf("\n--- compiled version and product constants\n");

    const auto version = expectedVersion();

    expectEqual(chordengine::licensing::productVersion, version,
                "licensing telemetry version is the compiled product version");
    expectEqual(chordengine::updates::currentBuildVersionString, version,
                "the update client's build version is the compiled product version");
    expectEqual(juce::String(JucePlugin_VersionString), version,
                "the plug-in's compiled version is the release version");

    expectEqual(chordengine::licensing::productSlug, expectedProductSlug,
                "licensing uses the shared product slug");
    expectEqual(chordengine::updates::productSlug, expectedProductSlug,
                "the update client uses the same product slug");
    expectEqual(chordengine::licensing::deviceName, expectedPluginName,
                "the account bridge reports the ChordEngine device name");

    const auto url = chordengine::updates::buildUpdateUrl(
        juce::String(chordengine::licensing::authOrigin), expectedProductSlug, version,
        chordengine::updates::platformName());

    require(url.contains("current=" + version),
            "the update request carries the release version: " + url);
    require(url.contains("product=" + juce::String(expectedProductSlug)),
            "the update request carries the product slug");
    require(! url.contains("0.4.0"),
            "the update request never reports the reference product's 0.4.0");
}

// ------------------------------------------------------------- bundle lookup
juce::File newestBundleNamed(const juce::String& bundleName, const juce::String& overrideVariable)
{
    const auto fromEnvironment
        = juce::SystemStats::getEnvironmentVariable(overrideVariable, {});

    if (fromEnvironment.isNotEmpty())
        return juce::File(fromEnvironment);

    const juce::File buildDir { CHORDENGINE_BUILD_DIR };

    if (! buildDir.isDirectory())
        return {};

    juce::Array<juce::File> found;
    buildDir.findChildFiles(found, juce::File::findDirectories, true, bundleName);
    found.removeIf([](const juce::File& file) { return file.getFullPathName().contains("/_deps/"); });

    juce::File newest;

    for (const auto& file : found)
        if (newest == juce::File() || file.getLastModificationTime() > newest.getLastModificationTime())
            newest = file;

    return newest;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    std::printf("ChordEngine release-metadata gate - expected version %s\n",
                expectedVersion().toRawUTF8());

    std::vector<juce::File> bundles;

    for (int i = 1; i < argc && i <= 2; ++i)
        bundles.emplace_back(juce::String(argv[i]));
    const juce::File instrument = argc > 3
        ? juce::File(juce::String(argv[3]))
        : newestBundleNamed("ChordEngine Instrument.component", "CHORDENGINE_INSTRUMENT_AU_BUNDLE");

    if (bundles.size() < 2)
    {
        juce::File au = bundles.size() > 0
                            ? bundles[0]
                            : newestBundleNamed("ChordEngine.component", "CHORDENGINE_AU_BUNDLE");
        juce::File vst3 = bundles.size() > 1
                              ? bundles[1]
                              : newestBundleNamed("ChordEngine.vst3", "CHORDENGINE_VST3_BUNDLE");

        bundles = { au, vst3 };
    }

    checkCompiledConstants();

    int presentBundles = 0;

    const auto au = bundles[0];
    const auto vst3 = bundles[1];

    if (au.isDirectory())
    {
        ++presentBundles;
        checkAuMetadata(au, midiEffectAu);
        checkBundleSignature(au, "AU");
    }
    else
    {
        note("SKIP  no built AU bundle found (looked for " + au.getFullPathName() + ")");
    }

    if (instrument.isDirectory())
    {
        ++presentBundles;
        checkAuMetadata(instrument, instrumentAu);
        checkBundleSignature(instrument, "Instrument AU");
    }
    else
    {
        note("SKIP  no built Instrument AU bundle found (looked for "
             + instrument.getFullPathName() + ")");
    }

    if (vst3.isDirectory())
    {
        ++presentBundles;
        checkVst3Metadata(vst3);
        checkBundleSignature(vst3, "VST3");
    }
    else
    {
        note("SKIP  no built VST3 bundle found (looked for " + vst3.getFullPathName() + ")");
    }

    if (presentBundles > 0)
    {
        juce::AudioPluginFormatManager formats;
        juce::addHeadlessDefaultFormatsToManager(formats);

        if (vst3.isDirectory())
            checkHostedBundle(formats, vst3, "VST3", true, false, 1);

        if (au.isDirectory())
            checkHostedBundle(formats, au, "AU", false, true, 0);
    }

    if (presentBundles == 0)
    {
        std::printf("\nChordEngineReleaseMetadataTests: no built plug-in bundle was found "
                    "(build the AU and VST3 targets, or pass their paths)\n");
        return 77;
    }

    if (failures != 0)
    {
        std::printf("\nChordEngineReleaseMetadataTests: %d of %d check(s) failed\n", failures, checksRun);
        return 1;
    }

    std::printf("\nChordEngineReleaseMetadataTests: all %d release-metadata checks passed\n", checksRun);
    return 0;
}
