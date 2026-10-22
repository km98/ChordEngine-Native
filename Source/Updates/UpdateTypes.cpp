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

bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

// One numeric component of a semver core: at least one digit, no leading zero
// unless it is exactly "0".
bool isNumericIdentifier(const std::string& text) noexcept
{
    if (text.empty())
        return false;

    for (std::size_t index = 0; index < text.size(); ++index)
        if (!isDigit(text[index]))
            return false;

    if (text.size() > 1 && text[0] == '0')
        return false;

    return true;
}

bool isAlphanumericIdentifier(const std::string& text) noexcept
{
    if (text.empty())
        return false;

    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const auto c = text[index];
        if (!isDigit(c) && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && c != '-')
            return false;
    }

    return true;
}
} // namespace

std::string platformName()
{
#if defined(_WIN32) || defined(_WIN64)
    return "windows";
#else
    return "macos";
#endif
}

std::string buildUpdateUrl(const std::string& origin, const std::string& product,
                            const std::string& currentVersion, const std::string& platform)
{
    // Every value below is a validated, constrained token (a fixed slug, a
    // validated semver, a fixed platform word), so no escaping is required and
    // the URL stays exactly reproducible in the tests.
    return origin + updateFunctionPath
         + "?product=" + product
         + "&current=" + currentVersion
         + "&platform=" + platform;
}

bool isValidSemver(const std::string& text)
{
    if (text.empty())
        return false;

    if (text[0] == 'v' || text[0] == 'V')
        return false;

    auto core = text;
    auto preRelease = std::string();
    auto build = std::string();

    const auto plus = text.find('+');
    if (plus != std::string::npos)
    {
        build = text.substr(plus + 1);
        core = text.substr(0, plus);
    }

    const auto minus = core.find('-');
    if (minus != std::string::npos)
    {
        preRelease = core.substr(minus + 1);
        core = core.substr(0, minus);
    }

    // MAJOR.MINOR.PATCH - exactly three numeric components.
    const auto parts = text::split(core, '.');
    if (parts.size() != 3)
        return false;
    for (const auto& part : parts)
        if (!isNumericIdentifier(part))
            return false;

    // -prerelease: dot-separated alphanumeric identifiers.
    if (minus != std::string::npos)
    {
        const auto identifiers = text::split(preRelease, '.');
        if (identifiers.empty())
            return false;
        for (const auto& identifier : identifiers)
            if (!isAlphanumericIdentifier(identifier))
                return false;
    }

    // +build: dot-separated alphanumeric identifiers.
    if (plus != std::string::npos)
    {
        const auto identifiers = text::split(build, '.');
        if (identifiers.empty())
            return false;
        for (const auto& identifier : identifiers)
            if (!isAlphanumericIdentifier(identifier))
                return false;
    }

    return true;
}

std::string currentVersionLine(const UpdateView& view)
{
    return "Current version  " + view.currentVersion;
}

std::string latestVersionLine(const UpdateView& view)
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
                 + (!view.latestVersion.empty() ? view.latestVersion : std::string("-"));
        }
    }

    return "Latest version  -";
}

std::string statusLine(const UpdateView& view)
{
    std::string base;
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
            base = !view.detail.empty() ? view.detail
                                            : std::string("Error checking for updates");
            break;
    }

    // A restored answer is marked as cached; the offline line already says so.
    if (view.cached && view.status != UpdateStatus::offlineCached)
        base += " (cached)";

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
