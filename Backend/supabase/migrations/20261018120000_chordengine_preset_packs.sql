-- ChordEngine Preset Packs: catalog table + private delivery bucket.
--
-- NOT APPLIED ANYWHERE YET. Written in the ChordEngine-Native repo
-- (Backend/) to be ported into the music-prod.com Supabase project; see
-- Backend/README.md.
--
-- Served by supabase/functions/chordengine-packs:
--   * catalog  : public list of status = 'released' rows.
--   * download : plugin token (vyre_plugin_tokens, product 'chordengine') +
--                Music-Prod+ membership (has_active_subscription OR
--                lifetime_memberships) => a 10-minute signed URL for
--                storage_path in the private `chordengine-packs` bucket.
--
-- The table is server-only: RLS on, no policy for anon/authenticated. The
-- edge function reads it with the service role.

-- 1. Private bucket. No storage.objects policy is created: a private bucket
--    plus service-role signing is the whole policy.
INSERT INTO storage.buckets (id, name, public)
VALUES ('chordengine-packs', 'chordengine-packs', false)
ON CONFLICT (id) DO NOTHING;

-- 2. Catalog.
CREATE TABLE IF NOT EXISTS public.chordengine_preset_packs (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  slug TEXT NOT NULL UNIQUE
    CHECK (slug ~ '^[a-z0-9][a-z0-9-]{1,62}$'),
  name TEXT NOT NULL CHECK (char_length(name) BETWEEN 1 AND 64),
  description TEXT NOT NULL DEFAULT '' CHECK (char_length(description) <= 512),
  version TEXT NOT NULL CHECK (version ~ '^[0-9]+\.[0-9]+\.[0-9]+$'),
  preset_count INTEGER NOT NULL CHECK (preset_count BETWEEN 1 AND 64),
  is_stock BOOLEAN NOT NULL DEFAULT false,
  status TEXT NOT NULL DEFAULT 'draft' CHECK (status IN ('draft', 'released')),
  sort_order INTEGER NOT NULL DEFAULT 100,
  -- Object key inside the private `chordengine-packs` bucket.
  storage_path TEXT NOT NULL,
  -- Lowercase hex SHA-256 of the exact uploaded bytes; the plug-in refuses
  -- to install a file that does not hash to it.
  sha256 TEXT NOT NULL CHECK (sha256 ~ '^[0-9a-f]{64}$'),
  size_bytes INTEGER NOT NULL CHECK (size_bytes BETWEEN 1 AND 262144),
  artwork_url TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS chordengine_preset_packs_status_sort_idx
  ON public.chordengine_preset_packs (status, sort_order);

CREATE TRIGGER update_chordengine_preset_packs_updated_at
BEFORE UPDATE ON public.chordengine_preset_packs
FOR EACH ROW EXECUTE FUNCTION public.update_updated_at_column();

-- 3. Deny by default.
ALTER TABLE public.chordengine_preset_packs ENABLE ROW LEVEL SECURITY;
REVOKE ALL ON public.chordengine_preset_packs FROM anon, authenticated;

COMMENT ON TABLE public.chordengine_preset_packs IS
  'ChordEngine Preset Pack catalog. Server-only (RLS, no policies); read by supabase/functions/chordengine-packs with the service role.';

-- 4. Seed the two launch packs. sha256 / size_bytes are those of
--    Resources/Packs/<slug>.json in ChordEngine-Native; upload those exact
--    files to <bucket>/<storage_path> before releasing (see Backend/README.md).
INSERT INTO public.chordengine_preset_packs
  (slug, name, description, version, preset_count, is_stock, status, sort_order, storage_path, sha256, size_bytes)
VALUES
  ('chordengine-stock', 'ChordEngine Stock Pack', 'The 12 presets built into ChordEngine.',
   '1.0.0', 12, true, 'released', 10, 'chordengine-stock/1.0.0/chordengine-stock.json',
   'cb302adcbbc2f45502f845e8e0905deacd70919760b6bb08870bb894bd9ef8d1', 4522),
  ('chordengine-test-pack', 'ChordEngine Test Pack',
   'Four test presets that prove downloadable Preset Packs work end to end.',
   '1.0.0', 4, false, 'released', 20, 'chordengine-test-pack/1.0.0/chordengine-test-pack.json',
   'd3466889ac0175b73ba0ebd096551446d755fc6d646bbb7de4097801eb0e35bc', 1774)
ON CONFLICT (slug) DO NOTHING;
