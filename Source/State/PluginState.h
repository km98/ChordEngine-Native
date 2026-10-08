#pragma once

#include "../Core/ChordEngineCore.h"

namespace chordengine::state
{
// In-memory UI/processor state for the exposed musical controls. Serialization
// remains deferred until a host-state compatibility contract is approved.
// The recorder state lives in ChordEngineAudioProcessor (it is driven by the
// generated-note emission path) and the account/trial/update state lives in
// chordengine::licensing::LicensingBoundary (message thread only), so neither
// belongs in this musical-configuration snapshot.
struct PluginState
{
    chordengine::core::CoreConfiguration configuration{};
};
} // namespace chordengine::state
