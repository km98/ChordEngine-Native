#pragma once

#include "LicensingServices.h"

#include <juce_core/juce_core.h>

namespace chordengine::licensing
{
// The production AuthService: the EXISTING Music-Prod plugin auth function
// (functions/v1/vyre-plugin-auth) called with JSON POSTs, exactly as the
// reference bridge does. No API key, no secret, no user id and no email are
// sent - identity is resolved server-side from the opaque bearer token.
//
// Every request runs on a private background thread and the reply is
// delivered back on the message thread, so neither the audio thread nor the
// GUI thread is ever blocked by the network.
class MusicProdAuthService final : public AuthService
{
public:
    MusicProdAuthService();
    ~MusicProdAuthService() override;

    void start(const juce::String& deviceName, const juce::String& pluginVersion,
               Callback callback) override;
    void poll(const juce::String& deviceCode, Callback callback) override;
    void entitlements(const juce::String& token, Callback callback) override;
    void logout(const juce::String& token, Callback callback) override;

private:
    void post(const juce::var& body, Callback callback);

    juce::ThreadPool pool_ { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusicProdAuthService)
};
} // namespace chordengine::licensing
