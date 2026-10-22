# ChordEngine Preset Pack backend

> **NOT DEPLOYED.** Nothing in this folder has been applied to any Supabase
> project. It is staged here so the plug-in and its server contract can be
> developed and reviewed together, and is meant to be ported into the
> music-prod.com Supabase repository. Nothing here contains a secret.

| File | Purpose |
| --- | --- |
| `supabase/functions/chordengine-packs/index.ts` | Deno edge function: public catalog + member-only signed downloads |
| `supabase/migrations/20261018120000_chordengine_preset_packs.sql` | `chordengine_preset_packs` table (RLS, no policies), private `chordengine-packs` bucket, seed rows for the stock and test packs |

The wire contract and the pack format are documented in
[`Docs/PRESET_PACKS.md`](../Docs/PRESET_PACKS.md). `Tools/LocalPackServer.py`
emulates this function locally with the same contract.

## What it relies on in the music-prod.com project

These already exist there (they are what `vyre-plugin-auth` uses), and are not
redefined here:

- `public.vyre_plugin_tokens` (`token_hash`, `user_id`, `revoked_at`, `product`,
  `last_seen_at`): the plug-in token minted by `vyre-plugin-auth` `poll`. The
  function requires `product = 'chordengine'`.
- `public.has_active_subscription(_user_id)` RPC and `public.lifetime_memberships`:
  membership = either one.
- `public.user_roles` (role `admin`): admins may download `draft` packs for testing.
- `public.update_updated_at_column()` trigger function.
- `supabase/functions/_shared/cors.ts` (`getCorsHeaders`, `handleCorsPreflightRequest`).
- Environment: `SUPABASE_URL`, `SUPABASE_SERVICE_ROLE_KEY` (set by the
  platform; the same names the existing functions read).

## Port steps

1. Copy `supabase/functions/chordengine-packs/` into the website repo's
   `supabase/functions/`.
2. Copy the migration into `supabase/migrations/` (rename the timestamp if it
   no longer sorts last).
3. Add to `supabase/config.toml`, like `vyre-plugin-auth` / `vyre-pack-catalog`:
   ```toml
   [functions.chordengine-packs]
   verify_jwt = false
   ```
4. Review the seed rows. Both packs are seeded `released`; set the test pack to
   `draft` if it should not be public (admins can still download drafts).

## Deploy steps (when approved)

1. Apply the migration: `supabase db push` (or the project's usual migration flow).
2. Upload each pack file to the private bucket at the seeded `storage_path`,
   byte-for-byte (do not re-save or re-format the JSON: the plug-in checks the
   SHA-256):
   - `Resources/Packs/chordengine-stock.json` → `chordengine-packs/chordengine-stock/1.0.0/chordengine-stock.json`
   - `Resources/Packs/chordengine-test-pack.json` → `chordengine-packs/chordengine-test-pack/1.0.0/chordengine-test-pack.json`
3. Deploy the function: `supabase functions deploy chordengine-packs`.
4. Smoke test (no token needed):
   `curl "$SUPABASE_URL/functions/v1/chordengine-packs?action=catalog"`

## Publishing a new pack or version

1. Validate the JSON with the plug-in's validator (run `ChordEnginePackTests`
   with the file added to `Resources/Packs`, or parse it with `packs::parse`).
2. `shasum -a 256 <file>` and `wc -c <file>`.
3. Upload to `chordengine-packs/<slug>/<version>/<slug>.json` (a new path per
   version, so a signed URL for an old version never serves new bytes).
4. Insert or update the row: `version`, `preset_count`, `storage_path`,
   `sha256`, `size_bytes`, then `status = 'released'`. Installed copies show
   "Update available" when the catalog version is higher.
