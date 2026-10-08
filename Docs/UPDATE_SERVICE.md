# Native update service

The plugin-facing update client for ChordEngine. It is a self-contained module
in `Source/Updates/` and it is deliberately **separate from licensing**: it
reads no account or licence state, and the licensing subsystem never reads
update state.

## Scope

* **In scope** — asking the existing Music-Prod release service whether a newer
  ChordEngine build has been published, and reporting the answer honestly on the
  INFO page.
* **Out of scope (on purpose)** — downloading, installing, authorising a
  download (`/downloads/authorize`), signing, notarising, packaging or
  publishing. Installing an update belongs to Music-Prod Studio; a successful
  check never opens it and never starts a download.

## Endpoint

```
GET https://wfpeajmdojcjqyrsnxbk.supabase.co/functions/v1/music-prod-studio-api/updates
      ?product=chordengine
      &current=<actual build version>
      &platform=macos|windows
```

* Anonymous. No token, no API key, no secret. The origin is the shared
  Music-Prod origin constant (`chordengine::licensing::authOrigin`); the path and
  the product slug are owned by this module (`productSlug`, `updateFunctionPath`).
* `platform` is **always** sent explicitly.
* `current` is **always** the version of the binary that is running, taken from
  the build metadata (`JucePlugin_VersionString`, defined by the plugin target).
  Nothing in the client hardcodes a product version, and the UI shows the same
  value it sends.
* The `/updates` response carries **metadata only** — there is no download URL
  in it.

## Response handling

The payload is never trusted field-by-field. `parseUpdateResponse()` refuses a
response unless:

* `success` is exactly `true`;
* `data.product` is `chordengine`;
* `currentVersion`, when supplied, is valid semver;
* `latestVersion`, when non-null, is valid semver;
* `platform`, when the server reports one, matches the requested platform;
* the decision is one of `upToDate`, `optionalUpdate`, `mandatoryUpdate`,
  `belowMinimumSupported`;
* the payload is internally consistent (no update announced without a version,
  no `noPublishedRelease` alongside a version, no `updateAvailable` with an
  `upToDate` decision).

`status: "noPublishedRelease"` is classified on its own and is **never** mapped
to "Up to date"; the live `chordengine` answer (`decision: upToDate`,
`latestVersion: null`) renders as *No published release* on both the Latest and
the Status rows.

## States

| State | Latest row | Status row |
| --- | --- | --- |
| checking | `Checking...` | `Checking...` |
| up to date | the published version | `Up to date` |
| update available | the published version | `Update available` |
| mandatory update | the published version | `Update required` |
| no published release | `No published release` | `No published release` |
| offline (with a cached answer) | the cached answer | `Offline - showing cached result` |
| error / offline (nothing cached) | `-` | `Offline - could not reach the update service` or the error reason |

Every row also shows `Current version  <actual build version>`. A restored
answer is marked `(cached)`.

## Threading

* `UpdateService` owns `juce::ThreadPool pool_ { 1 }` — exactly one background
  worker, created once, never per check.
* Every request runs on that worker; the result is delivered back with
  `MessageManager::callAsync` and only then published.
* The audio/MIDI thread never sees this class: `processBlock` performs no
  network work, and neither the processor nor its header includes the module.
* Checks are started on editor open (once) and on an explicit
  `CHECK FOR UPDATES` click. Only one check can be in flight at a time.

## Timeouts and retry

* 5 s connect timeout, 5 s read deadline (JUCE exposes a single socket timeout,
  so the read deadline is enforced on top of it with a bounded read loop).
* The **only** retried failure is the release backend being temporarily
  unavailable (HTTP 503, or the server's `releaseBackendUnavailable` error
  code), retried **once**. An unreachable service is never retried in a loop.
* Any other failure produces an honest offline/error state and keeps the last
  successful answer. A failure is never reported as "Up to date".

## Cache

`update-cache.json` sits next to the plugin's other Music-Prod state
(`~/Library/Application Support/Music-Prod/ChordEngine/`). It stores only the
normalised public fields of the last successful answer plus the timestamp:
product, platform, build version, latest version, decision, status, reason,
mandatory flag and `checked_at`. No account, session, licence or usage data is
ever written, and no opaque identifiers are copied out of the response.

On editor open the cached answer is shown immediately (marked `(cached)`) and
then refreshed exactly once. At most **one** automatic check happens per launch;
an explicit click may always request another.

## Tests

`ChordEngineUpdateTests` covers the strict validation, the decision mapping
(including `noPublishedRelease` never being "up to date"), the 503-only retry
policy, the cache, the state transitions, the single-worker threading model, the
licensing-isolation invariants and the fact that the actual build version is the
one sent and displayed — 16 deterministic cases, none of which touches the live
service.
