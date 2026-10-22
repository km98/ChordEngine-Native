/**
 * ChordEngine Preset Pack store.
 *
 * NOT DEPLOYED. Lives in the ChordEngine-Native repo until it is ported into
 * the music-prod.com Supabase project (see Backend/README.md). Modelled on
 * vyre-pack-catalog (public catalog) and vyre-plugin-auth (token lookup,
 * membership rule, signed private downloads).
 *
 *   GET  ?action=catalog                      (or POST { action: "catalog" })
 *        => { version: 1, generated_at, packs: [{ slug, name, description, version,
 *             preset_count, is_stock, size_bytes, sha256, artwork_url?, sort_order }] }
 *   POST { action: "download", token, slug }
 *        200 { ok: true, slug, version, sha256, download_url }   (signed, 10 minutes)
 *        401 { error: "unauthorized" }        token unknown / revoked / not a ChordEngine token
 *        403 { error: "membership_required", unlock_url, unlock_message }
 *        404 { error: "not_found" }           no released pack with that slug (or file missing)
 *
 * The rule (owner decision): preset packs are for active Music-Prod+ members,
 * and a member may download EVERY released pack. Membership is the same test
 * vyre-plugin-auth applies: has_active_subscription(user) || lifetime_memberships.
 * The stock pack is listed too, although the plug-in already has it built in.
 *
 * verify_jwt = false (the plug-in sends no Supabase JWT and embeds no key);
 * identity comes only from the opaque plugin token, resolved by its SHA-256.
 */
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.57.4";
import { getCorsHeaders, handleCorsPreflightRequest } from "../_shared/cors.ts";

const PRODUCT = "chordengine";
const BUCKET = "chordengine-packs";
const SIGNED_URL_TTL_SECONDS = 600;
const UNLOCK_URL = "https://music-prod.com/plus";
const UNLOCK_MESSAGE = "Preset packs are included with Music-Prod+. Join at music-prod.com/plus";
const SLUG_PATTERN = /^[a-z0-9][a-z0-9-]{1,62}$/;
const MAX_BODY_BYTES = 16 * 1024;

const CATALOG_COLUMNS =
  "slug, name, description, version, preset_count, is_stock, size_bytes, sha256, artwork_url, sort_order";

function admin() {
  return createClient(
    Deno.env.get("SUPABASE_URL")!,
    Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
    { auth: { persistSession: false } },
  );
}

async function sha256(value: string): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value));
  return Array.from(new Uint8Array(digest)).map((b) => b.toString(16).padStart(2, "0")).join("");
}

export interface CatalogRow {
  slug: string;
  name: string;
  description: string | null;
  version: string;
  preset_count: number;
  is_stock: boolean;
  size_bytes: number;
  sha256: string;
  artwork_url: string | null;
  sort_order: number;
}

/** Wire shape of one catalog entry. `artwork_url` is omitted when unset. */
export function catalogEntry(row: CatalogRow) {
  return {
    slug: row.slug,
    name: row.name,
    description: row.description ?? "",
    version: row.version,
    preset_count: row.preset_count,
    is_stock: Boolean(row.is_stock),
    size_bytes: row.size_bytes,
    sha256: row.sha256,
    ...(row.artwork_url ? { artwork_url: row.artwork_url } : {}),
    sort_order: row.sort_order,
  };
}

/** A storage path is a plain object key: no scheme, no "..", no empty segment. */
export function isSafeStoragePath(path: unknown): path is string {
  if (typeof path !== "string" || path.length === 0 || path.length > 512) return false;
  if (/^[a-z][a-z0-9+.-]*:/i.test(path)) return false;
  return !path.split("/").some((segment) => segment === "" || segment === "." || segment === "..");
}

async function parseBody(req: Request): Promise<Record<string, unknown>> {
  const text = await req.text().catch(() => "");
  if (!text || text.length > MAX_BODY_BYTES) return {};
  try {
    const parsed = JSON.parse(text);
    return parsed && typeof parsed === "object" && !Array.isArray(parsed) ? parsed : {};
  } catch {
    return {};
  }
}

/**
 * The same lookup vyre-plugin-auth uses (resolveToken): the token is stored
 * only as its SHA-256 in vyre_plugin_tokens, and the product it was ISSUED for
 * is part of the row. A token minted for another product (VYRE, SideChain)
 * is not a ChordEngine identity, so it is answered exactly like an unknown one.
 */
async function resolveChordEngineToken(supabase: ReturnType<typeof admin>, token: unknown) {
  if (typeof token !== "string" || token.length < 32) return null;
  const { data } = await supabase
    .from("vyre_plugin_tokens")
    .select("id, user_id, revoked_at, product")
    .eq("token_hash", await sha256(token))
    .maybeSingle();
  if (!data || data.revoked_at || data.product !== PRODUCT) return null;
  await supabase
    .from("vyre_plugin_tokens")
    .update({ last_seen_at: new Date().toISOString() })
    .eq("id", data.id);
  return data;
}

Deno.serve(async (req) => {
  const origin = req.headers.get("origin");
  if (req.method === "OPTIONS") return handleCorsPreflightRequest(origin);

  const cors = { ...getCorsHeaders(origin), "Content-Type": "application/json" };
  const json = (body: unknown, status = 200, extra: Record<string, string> = {}) =>
    new Response(JSON.stringify(body), { status, headers: { ...cors, ...extra } });

  try {
    const url = new URL(req.url);
    const payload = req.method === "POST" ? await parseBody(req) : {};
    const action = String(payload.action ?? url.searchParams.get("action") ?? "");
    const supabase = admin();

    // -------------------------------------------------------------- catalog
    if (action === "catalog") {
      const { data, error } = await supabase
        .from("chordengine_preset_packs")
        .select(CATALOG_COLUMNS)
        .eq("status", "released")
        .order("sort_order", { ascending: true })
        .order("name", { ascending: true });
      if (error) throw error;

      return json(
        {
          version: 1,
          generated_at: new Date().toISOString(),
          packs: (data ?? []).map((row) => catalogEntry(row as CatalogRow)),
        },
        200,
        { "Cache-Control": "public, max-age=300, stale-while-revalidate=3600" },
      );
    }

    // ------------------------------------------------------------- download
    if (action === "download") {
      if (req.method !== "POST") return json({ error: "invalid_request" }, 405);

      // 1. Identity. Missing, unknown, revoked and other-product tokens are
      //    the same answer: the plug-in has to sign in again.
      const session = await resolveChordEngineToken(supabase, payload.token);
      if (!session) return json({ error: "unauthorized" }, 401);

      // 2. Membership - the vyre-plugin-auth rule, unchanged. Admins may also
      //    fetch DRAFT packs for internal testing (server-side only: drafts
      //    never appear in the catalog).
      const [{ data: subActive }, { data: lifetime }, { data: adminRow }] = await Promise.all([
        supabase.rpc("has_active_subscription", { _user_id: session.user_id }),
        supabase.from("lifetime_memberships").select("id").eq("user_id", session.user_id).maybeSingle(),
        supabase.from("user_roles").select("id").eq("user_id", session.user_id).eq("role", "admin").maybeSingle(),
      ]);
      const isAdmin = Boolean(adminRow);
      const isMember = Boolean(subActive) || Boolean(lifetime);
      if (!isMember && !isAdmin) {
        return json({ error: "membership_required", unlock_url: UNLOCK_URL, unlock_message: UNLOCK_MESSAGE }, 403);
      }

      // 3. The pack.
      const slug = typeof payload.slug === "string" ? payload.slug : "";
      if (!SLUG_PATTERN.test(slug)) return json({ error: "not_found" }, 404);

      const { data: pack } = await supabase
        .from("chordengine_preset_packs")
        .select("slug, version, status, storage_path, sha256")
        .eq("slug", slug)
        .maybeSingle();
      if (!pack || (pack.status !== "released" && !isAdmin)) return json({ error: "not_found" }, 404);
      if (!isSafeStoragePath(pack.storage_path)) {
        console.error("chordengine-packs: unsafe storage_path for", slug);
        return json({ error: "not_found" }, 404);
      }

      // 4. A short-lived signed URL into the PRIVATE bucket. Never a permanent
      //    or public link: one member's URL must stop working within minutes.
      const { data: signed, error: signError } = await supabase.storage
        .from(BUCKET)
        .createSignedUrl(pack.storage_path, SIGNED_URL_TTL_SECONDS);
      if (signError || !signed?.signedUrl) {
        console.error("chordengine-packs signed URL error", signError?.message);
        return json({ error: "not_found" }, 404);
      }

      return json({
        ok: true,
        slug: pack.slug,
        version: pack.version,
        sha256: pack.sha256,
        download_url: signed.signedUrl,
      });
    }

    return json({ error: "unknown_action" }, 400);
  } catch (error) {
    console.error("chordengine-packs error", error);
    return json({ error: "server_error" }, 500);
  }
});
