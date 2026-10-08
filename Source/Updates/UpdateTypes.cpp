#include "UpdateTypes.h"

namespace chordengine::updates
{
namespace
{
// Reference palette values used by the INFO page (the same hues the licensing
// page uses). Kept local so this module carries no licensing dependency.
constexpr std::uint32_t colourMuted = 0xff8a93a0;
constexpr std::uint32_t colourAmber = 0xffe0b050;
constexpr std::uint32_t colourGreen = 0xff7fd48a;
constexpr std::uint32_t colourRed = 0xffe05a5a;

bool isDigit(juce::juce_wchar c) noexcept
{
    return c >= '0' && c <= '9';
}

// One numeric component of a semver core: at least one digit, no leading zero
// unless it is exactly "0".
bool isNumericIdentifier(const juce::String& text) noexcept
{
    if (text.isEmpty())
        return false;

    for (int index = 0; index < text.length(); ++index)
        if (!isDigit(text[index]))
            return false;

    if (text.length() > 1 && text[0] == '0')
        return false;

    return true;
}

bool isAlphanumericIdentifier(const juce::String& text) noexcept
{
    if (text.isEmpty())
        return false;

    for (int index = 0; index < text.length(); ++index)
    {
        const auto c = text[index];
        if (!isDigit(c) && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && c != '-')
            return false;
    }

    return true;
}
} // namespace

juce::String platformName()
{
#if defined(_WIN32) || defined(_WIN64)
    return "windows";
#else
    return "macos";
#endif
}

juce::String buildUpdateUrl(const juce::String& origin, const juce::String& product,
                            const juce::String& currentVersion, const juce::String& platform)
{
    // Every value below is a validated, constrained token (a fixed slug, a
    // validated semver, a fixed platform word), so no escaping is required and
    // the URL stays exactly reproducible in the tests.
    jassert(product.isNotEmpty() && platform.isNotEmpty());
    return origin + updateFunctionPath
         + "?product=" + product
         + "&current=" + currentVersion
         + "&platform=" + platform;
}

bool isValidSemver(const juce::String& text)
{
    if (text.isEmpty())
        return false;

    if (text.startsWith("v") || text.startsWith("V"))
        return false;

    auto core = text;
    auto preRelease = juce::String();
    auto build = juce::String();

    const auto plus = text.indexOfChar('+');
    if (plus >= 0)
    {
        build = text.substring(plus + 1);
        core = text.substring(0, plus);
    }

    const auto minus = core.indexOfChar('-');
    if (minus >= 0)
    {
        preRelease = core.substring(minus + 1);
        core = core.substring(0, minus);
    }

    // MAJOR.MINOR.PATCH - exactly three numeric components.
    const auto parts = juce::StringArray::fromTokens(core, ".", {});
    if (parts.size() != 3)
        return false;
    for (const auto& part : parts)
        if (!isNumericIdentifier(part))
            return false;

    // -prerelease: dot-separated alphanumeric identifiers.
    if (minus >= 0)
    {
        auto identifiers = juce::StringArray::fromTokens(preRelease, ".", {});
        if (identifiers.isEmpty())
            return false;
        for (const auto& identifier : identifiers)
            if (!isAlphanumericIdentifier(identifier))
                return false;
    }

    // +build: dot-separated alphanumeric identifiers.
    if (plus >= 0)
    {
        auto identifiers = juce::StringArray::fromTokens(build, ".", {});
        if (identifiers.isEmpty())
            return false;
        for (const auto& identifier : identifiers)
            if (!isAlphanumericIdentifier(identifier))
                return false;
    }

    return true;
}

juce::String currentVersionLine(const UpdateView& view)
{
    return "Current version  " + view.currentVersion;
}

juce::String latestVersionLine(const UpdateView& view)
{
    switch (view.status)
    {
        case UpdateStatus::idle:
            return "Latest version  -";
        case UpdateStatus::checking:
            return "Latest version  Checking...";
        case UpdateStatus::noPublishedRelease:
        case UpdateStatus::upToDate:
        case UpdateStatus::updateAvailable:
        case UpdateStatus::offlineCached:
        case UpdateStatus::error:
        {
            // A real answer (or the last real answer) is never replaced by a
            // placeholder dash. When the shown answer came from the cache, the
            // row reports what THAT answer said.
            const auto source = view.cached && view.cachedStatus != UpdateStatus::idle
                                    ? view.cachedStatus
                                    : view.status;
            if (source == UpdateStatus::noPublishedRelease)
                return "Latest version  No published release";

            return "Latest version  "
                 + (view.latestVersion.isNotEmpty() ? view.latestVersion : juce::String("-"));
        }
    }

    return "Latest version  -";
}

juce::String statusLine(const UpdateView& view)
{
    juce::String base;
    switch (view.status)
    {
        case UpdateStatus::idle: base = "-"; break;
        case UpdateStatus::checking: base = "Checking..."; break;
        case UpdateStatus::upToDate: base = "Up to date"; break;
        case UpdateStatus::updateAvailable:
            base = view.mustUpdate ? "Update required" : "Update available";
            break;
        case UpdateStatus::noPublishedRelease: base = "No published release"; break;
        case UpdateStatus::offlineCached: base = "Offline - showing cached result"; break;
        case UpdateStatus::error:
            // `detail` is already a complete, honest phrase (an offline note or
            // an error note); it is never a version.
            base = view.detail.isNotEmpty() ? view.detail
                                            : juce::String("Error checking for updates");
            break;
    }

    // A restored answer is marked as cached; the offline line already says so.
    if (view.cached && view.status != UpdateStatus::offlineCached)
        base << " (cached)";

    return "Status  " + base;
}

std::uint32_t statusColour(const UpdateView& view) noexcept
{
    switch (view.status)
    {
        case UpdateStatus::updateAvailable: return view.mustUpdate ? colourRed : colourGreen;
        case UpdateStatus::upToDate: return colourGreen;
        case UpdateStatus::noPublishedRelease: return colourAmber;
        case UpdateStatus::offlineCached: return colourAmber;
        case UpdateStatus::error: return colourRed;
        case UpdateStatus::checking:
        case UpdateStatus::idle:
        default: return colourMuted;
    }
}
} // namespace chordengine::updates
