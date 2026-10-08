#include "LicensingServices.h"

namespace chordengine::licensing
{
UpdateService::Result MusicProdUpdateService::check()
{
    // Reference ceCheckForUpdates(): there is still NO plugin-facing update
    // API for ChordEngine, so the check reports honestly and directs the user
    // to Music-Prod Studio, which owns installation and updates. No endpoint
    // is invented and no latest version is fabricated.
    UpdateService::Result result;
    result.available = false;
    result.latestVersion = {};
    result.status = updateStatusUnavailable;
    return result;
}
} // namespace chordengine::licensing
