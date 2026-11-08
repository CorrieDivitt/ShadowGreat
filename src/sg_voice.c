/*
 * sg_voice.c - the narrator's voice (Phase 7). See sg_voice.h.
 *
 * Flow:
 *   sg_voice_say()   (game thread)  stores the text and wakes the voice thread
 *   voice thread                    turns it into speech one sentence at a
 *                                   time, adding each sentence to a buffer
 *   sg_voice_mix()   (game thread)  plays the buffer into the game's sound,
 *                                   slowed down by the preset's pitch factor
 * A new message cuts off the old one at once, and closing the box stops it.
 *
 * Starting sooner (p7-faststart): speech is made as soon as the message
 * starts (sg_voice_say) but held silent until the box is on screen
 * (sg_voice_release); a finished message is also kept in memory, so the
 * next time the same words come up they play at once.
 *
 * Pages: the text has '\f' where the game waits for A. Each page is made
 * separately and we note where it starts in the buffer; playback stops at
 * the end of the page the game is showing and goes on (jumping ahead if
 * need be) when the player turns the page (sg_voice_page).
 *
 * Loudness: Kokoro speaks quietly, so the voice is brought up to near full
 * scale (from the loudest moment heard so far), then VoiceVolume applies,
 * and a soft limiter keeps voice + music from crackling.
 */
#include "sg_voice.h"
#include "sg_features.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The three presets chosen by the user on 2026-11-08 (Phase 7 doc). */
const SgVoicePreset sg_voice_presets[] = {
    /* name        sid blend  mix   speed  pitch  pause */
    { "Narrator",  27,  -1,  0.0f,  1.22f, 0.85f, 0.5f },  /* bm_lewis, deeper   */
    { "Grim",      27,  14,  0.4f,  1.22f, 1.00f, 0.5f },  /* 60 % lewis + 40 % am_fenrir */
    { "Plain",     27,  -1,  0.0f,  1.00f, 1.00f, 0.5f },  /* bm_lewis as is     */
};
const int sg_voice_preset_count = (int)(sizeof sg_voice_presets / sizeof sg_voice_presets[0]);

#if defined(SG_HAVE_VOICE) && defined(CYC_WITH_SDL)

#include <SDL.h>
#include "sherpa-onnx/c-api/c-api.h"

bool sg_voice_built_in(void) { return true; }

#define KOKORO_STYLE_FLOATS (510 * 256)   /* one voice in voices.bin */

/* ---- shared between the two threads (guarded by s_lock) ---- */
static SDL_mutex *s_lock;
static SDL_cond  *s_wake;
static char      *s_pending;        /* text waiting to be spoken (owned)    */
static unsigned   s_request;        /* number of the newest request         */
static float     *s_pcm;            /* speech made so far for that request  */
static size_t     s_pcm_len, s_pcm_cap;
static double     s_pos;            /* playback position in s_pcm           */
static int        s_pcm_rate = 24000;
#define MAX_PAGES 64
static size_t     s_page_start[MAX_PAGES];   /* where each page starts in s_pcm */
static int        s_pages_known;             /* pages whose start is known      */
static int        s_shown_page;              /* page the game is showing        */
static int        s_play_page;               /* page being played               */
static float      s_peak = 0.2f;             /* loudest sample heard so far     */
static bool       s_hold;                    /* made but not to be played yet   */
static bool       s_from_memory;             /* this request came from the cache */
static bool       s_first_sound_logged;
static Uint32     s_release_ticks;           /* when the box appeared            */
static uint64_t   s_request_key;             /* cache key of this request        */

/* ---- speech already made, kept in memory (newest last) ----
 * Stored as 16-bit samples; about 2.9 MB per minute of speech. */
#define CACHE_MAX_ENTRIES 256
#define CACHE_MAX_SAMPLES ((size_t)24000 * 60 * 10)   /* ~10 minutes, ~29 MB */
typedef struct {
    uint64_t key;
    int16_t *pcm;
    size_t   len;
    int      pages;
    size_t   page_start[64];
    unsigned last_used;
} CacheEntry;
static CacheEntry s_cache[CACHE_MAX_ENTRIES];
static int        s_cache_count;
static size_t     s_cache_samples;
static unsigned   s_cache_clock;
static unsigned   s_cache_hits, s_cache_misses;

/* ---- set once at start ---- */
static bool           s_started;
static SgVoicePreset  s_preset;               /* the preset wanted now          */
static int            s_preset_index;
static int            s_volume = 80;
static float          s_music_duck = 0.4f;   /* game sound level while the voice talks */
static float          s_quote_pause = 0.8f;  /* seconds of quiet after closing a quote  */
static float          s_exclaim_pause = 0.6f;/* seconds of quiet after "!"              */
static float          s_duck = 1.0f;         /* current game sound level (fades)       */
static char           s_model_dir[1024];
static char           s_base[1024];         /* the .exe's folder, ends in a slash */

/* ---- small helpers ---- */
static void join(char *out, size_t n, const char *a, const char *b)
{
    snprintf(out, n, "%s%s", a, b);
}

static bool is_absolute(const char *p)
{
    return p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

/* ---- words the voice knows (to log the ones it would skip) ----
 * sherpa-onnx silently skips a word that is in none of its word lists.
 * We read the same lists and write any such word to shadowgate.log, once,
 * so it can be added to sg-names.txt. Only 64-bit fingerprints are kept. */
#define KNOWN_BITS 20
static uint64_t *s_known;               /* open addressing, 0 = empty */
static uint64_t  s_reported[512];
static int       s_reported_count;

static uint64_t fingerprint(const char *s, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= (uint8_t)s[i]; h *= 1099511628211ull; }
    return h ? h : 1;
}

static void known_add(uint64_t h)
{
    size_t mask = ((size_t)1 << KNOWN_BITS) - 1, i = (size_t)h & mask;
    while (s_known[i] && s_known[i] != h) i = (i + 1) & mask;
    s_known[i] = h;
}

static bool known_has(uint64_t h)
{
    size_t mask = ((size_t)1 << KNOWN_BITS) - 1, i = (size_t)h & mask;
    while (s_known[i]) { if (s_known[i] == h) return true; i = (i + 1) & mask; }
    return false;
}

static int load_word_list(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char line[512];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        size_t len = 0;
        while (line[len] && line[len] != ' ' && line[len] != '\t' && line[len] != '\r' && line[len] != '\n') {
            if (line[len] >= 'A' && line[len] <= 'Z') line[len] = (char)(line[len] - 'A' + 'a');
            ++len;
        }
        if (len) { known_add(fingerprint(line, len)); ++n; }
    }
    fclose(f);
    return n;
}

static bool word_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\'';
}

static void report_unknown_words(const char *text)
{
    if (!s_known) return;
    char w[64];
    for (const char *p = text; *p;) {
        if (!word_char(*p)) { ++p; continue; }
        size_t n = 0;
        while (word_char(*p)) { if (n + 1 < sizeof w) w[n++] = *p; ++p; }
        while (n && w[n - 1] == '\'') --n;              /* "SAYS'" -> "SAYS" */
        size_t s = 0;
        while (s < n && w[s] == '\'') ++s;
        if (s >= n) continue;
        for (size_t i = s; i < n; ++i)
            if (w[i] >= 'A' && w[i] <= 'Z') w[i] = (char)(w[i] - 'A' + 'a');
        uint64_t h = fingerprint(w + s, n - s);
        if (known_has(h)) continue;
        bool seen = false;
        for (int i = 0; i < s_reported_count; ++i) if (s_reported[i] == h) seen = true;
        if (seen) continue;
        if (s_reported_count < (int)(sizeof s_reported / sizeof s_reported[0]))
            s_reported[s_reported_count++] = h;
        w[n] = 0;
        sg_log("voice: unknown word \"%s\" (it will be skipped; add it to sg-names.txt)", w + s);
    }
}

/* ---- the text the voice gets ---- */
/* Marks we put in the text for the voice thread: a longer pause here. */
#define MARK_QUOTE_PAUSE   '\x01'   /* after the closing quote of spoken dialogue */
#define MARK_EXCLAIM_PAUSE '\x02'   /* after an exclamation mark                  */

/* Words the dictionaries say the wrong way in one place in the game. The
 * replacement is a made-up spelling with its own line in sg-names.txt.
 * Whole words only; the game's text is in capitals. */
static const struct { const char *from, *to; } PHRASE_FIXES[] = {
    /* "winds" as in a winding path (sg-names: wynds). The game also says
     * "THE SWIRLING WINDS", which stays as it is. Found by scanning every
     * WIND in the game's text, 2026-11-08. */
    { "PATH WINDS",    "PATH WYNDS" },      /* the prophecy: THE PATH WINDS ROUND */
    { "PASSAGE WINDS", "PASSAGE WYNDS" },   /* THE STONE PASSAGE WINDS TO AN UNSEEN END */
    { "SCRIPT WINDS",  "SCRIPT WYNDS" },    /* ...SCRIPT WINDS ITS WAY... */
};
#define PHRASE_FIXES_VERSION 2         /* bump when the list changes (memory key) */

static bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\''; }

static char *fix_phrases(const char *in)
{
    size_t n = strlen(in), cap = n * 2 + 16, o = 0;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    for (size_t i = 0; i < n;) {
        bool done = false;
        for (size_t f = 0; f < sizeof PHRASE_FIXES / sizeof PHRASE_FIXES[0] && !done; ++f) {
            size_t len = strlen(PHRASE_FIXES[f].from), tl = strlen(PHRASE_FIXES[f].to);
            if (strncmp(in + i, PHRASE_FIXES[f].from, len) == 0 &&
                (i == 0 || !letter(in[i - 1])) && !letter(in[i + len]) && o + tl < cap) {
                sg_log("voice: saying \"%s\" as \"%s\"", PHRASE_FIXES[f].from, PHRASE_FIXES[f].to);
                memcpy(out + o, PHRASE_FIXES[f].to, tl);
                o += tl;
                i += len;
                done = true;
            }
        }
        if (!done && o + 1 < cap) out[o++] = in[i++];
        else if (!done) i++;
    }
    out[o] = 0;
    return out;
}

static char *clean_text(const char *raw)
{
    char *in = fix_phrases(raw);
    if (!in) return NULL;
    size_t n = strlen(in);
    char *out = (char *)malloc(n * 2 + 1);
    if (!out) { free(in); return NULL; }
    size_t o = 0;
    bool in_quote = false;               /* quotes alternate open / close over the whole message */
    for (size_t i = 0; i < n; ++i) {
        char c = in[i];
        if (c == '"') {                   /* a quote glued to a word hides it from the word lists */
            in_quote = !in_quote;
            if (!in_quote) {              /* closing: pause after the dialogue */
                if (o && out[o - 1] == MARK_EXCLAIM_PAUSE) o--;   /* one pause, the longer one */
                out[o++] = MARK_QUOTE_PAUSE;
            }
            c = ' ';
        }
        if (c == '\f') {                  /* page end: no spaces around it */
            while (o && out[o - 1] == ' ') --o;
            out[o++] = c;
            continue;
        }
        if (c == ' ' && (o == 0 || out[o - 1] == ' ' || out[o - 1] == '\f')) continue;
        out[o++] = c;
        if (c == '!' && (i + 1 >= n || in[i + 1] != '!')) out[o++] = MARK_EXCLAIM_PAUSE;
    }
    while (o && out[o - 1] == ' ') --o;
    out[o] = 0;
    free(in);
    return out;
}

/* ---- Grim's blended voice ----
 * Kokoro's voices.bin holds one block of numbers per voice. A blend is just
 * a weighted average of two blocks. We write a copy of voices.bin beside the
 * .exe (sg-voices.bin) with the blend in the last slot, which is a voice for
 * another language that we never use. */
static bool make_blend_file(const char *voices_in, const char *voices_out, int *sid)
{
    FILE *f = fopen(voices_in, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t floats = (size_t)bytes / sizeof(float);
    int voices = (int)(floats / KOKORO_STYLE_FLOATS);
    float *v = (float *)malloc((size_t)bytes);
    bool ok = v && voices > 0 && s_preset.sid < voices && s_preset.blend_sid < voices &&
              fread(v, 1, (size_t)bytes, f) == (size_t)bytes;
    fclose(f);
    if (ok) {
        int slot = voices - 1;
        float *a = v + (size_t)s_preset.sid * KOKORO_STYLE_FLOATS;
        float *b = v + (size_t)s_preset.blend_sid * KOKORO_STYLE_FLOATS;
        float *d = v + (size_t)slot * KOKORO_STYLE_FLOATS;
        for (size_t k = 0; k < KOKORO_STYLE_FLOATS; ++k)
            d[k] = (1.0f - s_preset.blend) * a[k] + s_preset.blend * b[k];
        FILE *o = fopen(voices_out, "wb");
        ok = o && fwrite(v, 1, (size_t)bytes, o) == (size_t)bytes;
        if (o) fclose(o);
        if (ok) *sid = slot;
    }
    free(v);
    return ok;
}

/* ---- the voice thread ---- */
typedef struct { unsigned request; int page; size_t made; bool cut; } Job;

/* Add speech (or quiet, if samples is NULL) for this job. With s_lock held. */
static bool append_speech(Job *job, const float *samples, int32_t n)
{
    bool current = job->request == s_request && job->page >= s_shown_page;
    if (current && n > 0) {
        if (s_pcm_len + (size_t)n > s_pcm_cap) {
            size_t cap = s_pcm_cap ? s_pcm_cap : 24000 * 10;
            while (cap < s_pcm_len + (size_t)n) cap *= 2;
            float *p = (float *)realloc(s_pcm, cap * sizeof *p);
            if (p) { s_pcm = p; s_pcm_cap = cap; }
        }
        if (s_pcm_len + (size_t)n <= s_pcm_cap) {
            if (samples) memcpy(s_pcm + s_pcm_len, samples, (size_t)n * sizeof *samples);
            else memset(s_pcm + s_pcm_len, 0, (size_t)n * sizeof *s_pcm);
            s_pcm_len += (size_t)n;
        }
        job->made += (size_t)n;
    }
    if (!current) job->cut = true;
    return current;
}

/* After a piece ending in one of our pause marks: make the quiet at its end
 * last `seconds` as heard (Kokoro already leaves some; we top it up). */
static void pad_pause(Job *job, size_t piece_start, float seconds)
{
    SDL_LockMutex(s_lock);
    if (job->request == s_request) {
        size_t quiet = 0;
        while (s_pcm_len - quiet > piece_start && s_pcm[s_pcm_len - quiet - 1] < 0.002f &&
               s_pcm[s_pcm_len - quiet - 1] > -0.002f) quiet++;
        /* heard time = made time / pitch, so make seconds * pitch of it */
        size_t want = (size_t)(seconds * s_preset.pitch * (float)s_pcm_rate);
        if (want > quiet) append_speech(job, NULL, (int32_t)(want - quiet));
    }
    SDL_UnlockMutex(s_lock);
}

static int32_t on_speech(const float *samples, int32_t n, float progress, void *arg)
{
    (void)progress;
    Job *job = (Job *)arg;
    SDL_LockMutex(s_lock);
    /* Stop if a newer message came, or the player already turned past
     * this page. */
    bool current = append_speech(job, samples, n);
    if (current)
        for (int32_t i = 0; i < n; ++i) {
            float a = samples[i] < 0 ? -samples[i] : samples[i];
            if (a > s_peak) s_peak = a;
        }
    SDL_UnlockMutex(s_lock);
    return current ? 1 : 0;              /* 0 = stop making this page */
}

static const SherpaOnnxOfflineTts *load_model(int *sid)
{
    char model[1200], voices[1200], tokens[1200], lexicons[4000], ours[1200], blend[1200];
    join(model,  sizeof model,  s_model_dir, "/model.onnx");
    join(voices, sizeof voices, s_model_dir, "/voices.bin");
    join(tokens, sizeof tokens, s_model_dir, "/tokens.txt");
    join(ours,   sizeof ours,   s_base, "sg-names.txt");
    if (!file_exists(model) || !file_exists(voices) || !file_exists(tokens)) {
        sg_log("voice: no Kokoro model in \"%s\" (set VoiceModelFolder in config.ini)", s_model_dir);
        return NULL;
    }
    /* British first, US fills gaps, ours last: the LAST list wins. */
    snprintf(lexicons, sizeof lexicons, "%s/lexicon-us-en.txt,%s/lexicon-gb-en.txt%s%s",
             s_model_dir, s_model_dir, file_exists(ours) ? "," : "", file_exists(ours) ? ours : "");
    if (!file_exists(ours)) sg_log("voice: sg-names.txt is missing beside the .exe; names will be skipped");

    *sid = s_preset.sid;
    if (s_preset.blend_sid >= 0) {
        join(blend, sizeof blend, s_base, "sg-voices.bin");
        if (make_blend_file(voices, blend, sid)) snprintf(voices, sizeof voices, "%s", blend);
        else sg_log("voice: could not make the blended voice; using the plain one");
    }

    int cpus = SDL_GetCPUCount();
    int threads = cpus >= 8 ? 4 : cpus >= 4 ? 2 : 1;

    SherpaOnnxOfflineTtsConfig c;
    memset(&c, 0, sizeof c);
    c.model.kokoro.model = model;
    c.model.kokoro.voices = voices;
    c.model.kokoro.tokens = tokens;
    c.model.kokoro.lexicon = lexicons;
    c.model.kokoro.length_scale = 1.0f;
    c.model.num_threads = threads;
    c.model.provider = "cpu";
    c.max_num_sentences = 1;             /* one sentence at a time: speech starts sooner */
    c.silence_scale = s_preset.pause;

    Uint32 t0 = SDL_GetTicks();
    const SherpaOnnxOfflineTts *tts = SherpaOnnxCreateOfflineTts(&c);
    if (!tts) { sg_log("voice: the Kokoro model in \"%s\" did not load", s_model_dir); return NULL; }
    s_pcm_rate = SherpaOnnxOfflineTtsSampleRate(tts);
    sg_log("voice: ready - preset %s, %d threads, %d Hz, loaded in %u ms",
           s_preset.name, threads, s_pcm_rate, (unsigned)(SDL_GetTicks() - t0));

    /* The word lists, to report words the voice would skip. */
    if (!s_known) s_known = (uint64_t *)calloc((size_t)1 << KNOWN_BITS, sizeof *s_known);
    static bool words_loaded;
    if (s_known && !words_loaded) {
        words_loaded = true;
        char p[1200];
        int total = 0, n;
        join(p, sizeof p, s_model_dir, "/lexicon-us-en.txt"); if ((n = load_word_list(p)) > 0) total += n;
        join(p, sizeof p, s_model_dir, "/lexicon-gb-en.txt"); if ((n = load_word_list(p)) > 0) total += n;
        if ((n = load_word_list(ours)) > 0) total += n;
        sg_log("voice: %d dictionary entries", total);
    }
    return tts;
}

/* ---- the memory of finished messages ---- */
static uint64_t speech_key(const char *clean)
{
    /* The words plus everything that changes how they sound. */
    char extra[96];
    snprintf(extra, sizeof extra, "|%d|%d|%.3f|%.3f|%.3f|%.2f|%.2f|%d", s_preset.sid, s_preset.blend_sid,
             s_preset.blend, s_preset.speed, s_preset.pause, s_quote_pause, s_exclaim_pause,
             PHRASE_FIXES_VERSION);
    uint64_t a = fingerprint(clean, strlen(clean)), b = fingerprint(extra, strlen(extra));
    return (a ^ (b * 0x9E3779B97F4A7C15ull)) | 1;
}

static void forget_oldest(void)
{
    int oldest = 0;
    for (int i = 1; i < s_cache_count; ++i)
        if (s_cache[i].last_used < s_cache[oldest].last_used) oldest = i;
    s_cache_samples -= s_cache[oldest].len;
    free(s_cache[oldest].pcm);
    s_cache[oldest] = s_cache[--s_cache_count];
}

/* Called on the voice thread when request `request` was made in full. */
static void remember(uint64_t key, unsigned request)
{
    SDL_LockMutex(s_lock);
    if (request == s_request && s_pcm_len > 0 && s_pcm_len <= CACHE_MAX_SAMPLES) {
        while (s_cache_count && (s_cache_count >= CACHE_MAX_ENTRIES ||
                                 s_cache_samples + s_pcm_len > CACHE_MAX_SAMPLES))
            forget_oldest();
        int16_t *copy = (int16_t *)malloc(s_pcm_len * sizeof *copy);
        if (copy) {
            for (size_t i = 0; i < s_pcm_len; ++i) {
                float v = s_pcm[i] * 32767.0f;
                copy[i] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
            }
            CacheEntry *e = &s_cache[s_cache_count++];
            e->key = key;
            e->pcm = copy;
            e->len = s_pcm_len;
            e->pages = s_pages_known;
            memcpy(e->page_start, s_page_start, sizeof e->page_start);
            e->last_used = ++s_cache_clock;
            s_cache_samples += s_pcm_len;
        }
    }
    SDL_UnlockMutex(s_lock);
}

/* With s_lock held: play a remembered message. */
static bool recall(uint64_t key)
{
    for (int i = 0; i < s_cache_count; ++i) {
        CacheEntry *e = &s_cache[i];
        if (e->key != key) continue;
        if (e->len > s_pcm_cap) {
            float *p = (float *)realloc(s_pcm, e->len * sizeof *p);
            if (!p) return false;
            s_pcm = p;
            s_pcm_cap = e->len;
        }
        for (size_t k = 0; k < e->len; ++k) s_pcm[k] = e->pcm[k] / 32767.0f;
        s_pcm_len = e->len;
        memcpy(s_page_start, e->page_start, sizeof s_page_start);
        s_pages_known = e->pages;
        e->last_used = ++s_cache_clock;
        return true;
    }
    return false;
}

static int voice_thread(void *unused)
{
    (void)unused;
    int sid = 0, loaded = s_preset_index;
    const SherpaOnnxOfflineTts *tts = load_model(&sid);
    for (;;) {
        SDL_LockMutex(s_lock);
        while (!s_pending) SDL_CondWait(s_wake, s_lock);
        char *text = s_pending;
        s_pending = NULL;
        Job job = { s_request, 0, 0, false };
        uint64_t key = s_request_key;
        int want = s_preset_index;
        SDL_UnlockMutex(s_lock);

        /* The narrator voice was changed in the menu: load it (~1.5 s). */
        if (want != loaded) {
            if (tts) SherpaOnnxDestroyOfflineTts(tts);
            tts = load_model(&sid);
            loaded = want;
        }
        if (!tts) { free(text); continue; }

        report_unknown_words(text);
        SherpaOnnxGenerationConfig g;
        memset(&g, 0, sizeof g);
        g.sid = sid;
        g.speed = s_preset.speed;
        g.silence_scale = s_preset.pause;
        Uint32 t0 = SDL_GetTicks();
        int pages = 0;
        char *page = text;
        while (page && pages < MAX_PAGES) {
            char *next = strchr(page, '\f');
            if (next) *next++ = 0;
            if (!next && !*page) { page = NULL; break; }   /* nothing after the last page end */
            SDL_LockMutex(s_lock);
            bool stale = job.request != s_request;
            bool skip = job.page < s_shown_page;   /* the player is already past it */
            if (!stale) { s_page_start[job.page] = s_pcm_len; s_pages_known = job.page + 1; }
            SDL_UnlockMutex(s_lock);
            if (stale) { job.cut = true; break; }
            if (skip) job.cut = true;
            /* A page is made in pieces, split at our pause marks. */
            char *piece = page;
            while (!skip && piece && *piece) {
                char *mark = piece;
                while (*mark && *mark != MARK_QUOTE_PAUSE && *mark != MARK_EXCLAIM_PAUSE) mark++;
                char kind = *mark;
                if (*mark) *mark++ = 0;
                bool words = false;
                for (const char *c = piece; *c; ++c) if (*c != ' ') words = true;
                SDL_LockMutex(s_lock);
                size_t piece_start = s_pcm_len;
                SDL_UnlockMutex(s_lock);
                if (words) {
                    const SherpaOnnxGeneratedAudio *a =
                        SherpaOnnxOfflineTtsGenerateWithConfig(tts, piece, &g, on_speech, &job);
                    if (a) SherpaOnnxDestroyOfflineTtsGeneratedAudio(a);
                    SDL_LockMutex(s_lock);
                    bool gone = job.request != s_request || job.page < s_shown_page;
                    SDL_UnlockMutex(s_lock);
                    if (gone) break;              /* newer message, or the player turned the page */
                }
                bool more = false;            /* no pause at a page end: the game waits there anyway */
                for (const char *c = mark; kind && *c; ++c)
                    if (*c != ' ' && *c != MARK_QUOTE_PAUSE && *c != MARK_EXCLAIM_PAUSE) more = true;
                if (words && more)
                    pad_pause(&job, piece_start, kind == MARK_QUOTE_PAUSE ? s_quote_pause : s_exclaim_pause);
                piece = kind ? mark : NULL;
            }
            job.page++;
            pages++;
            page = next;
        }
        if (page) job.cut = true;                       /* more than MAX_PAGES */
        if (!job.cut) remember(key, job.request);
        sg_log("voice: made %.1f s of speech (%d pages) in %u ms%s",
               job.made / (double)s_pcm_rate, pages, (unsigned)(SDL_GetTicks() - t0),
               job.cut ? " (cut short, not kept)" : "");
        free(text);
    }
    return 0;   /* (never reached: the voice thread runs until the game closes) */
}

void sg_voice_start(int preset, const char *model_dir, int volume, int music)
{
    if (s_started) {                     /* again (reset, or switched back on) */
        sg_voice_stop();
        sg_voice_set_preset(preset);
        sg_voice_set_volume(volume, music);
        return;
    }
    s_music_duck = (music < 0 ? 0 : music > 100 ? 100 : music) / 100.0f;
    s_started = true;
    if (preset < 0 || preset >= sg_voice_preset_count) preset = 0;
    s_preset = sg_voice_presets[preset];
    s_preset_index = preset;
    s_volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    char *base = SDL_GetBasePath();
    snprintf(s_base, sizeof s_base, "%s", base ? base : "");
    if (base) SDL_free(base);
    if (!model_dir || !*model_dir) model_dir = "kokoro-multi-lang-v1_0";
    if (is_absolute(model_dir)) snprintf(s_model_dir, sizeof s_model_dir, "%s", model_dir);
    else snprintf(s_model_dir, sizeof s_model_dir, "%s%s", s_base, model_dir);
    size_t n = strlen(s_model_dir);
    while (n && (s_model_dir[n - 1] == '/' || s_model_dir[n - 1] == '\\')) s_model_dir[--n] = 0;

    s_lock = SDL_CreateMutex();
    s_wake = SDL_CreateCond();
    SDL_Thread *t = (s_lock && s_wake) ? SDL_CreateThread(voice_thread, "voice", NULL) : NULL;
    if (t) SDL_DetachThread(t);
    else sg_log("voice: could not start the voice thread");
    sg_log("voice: loading \"%s\" in the background", s_model_dir);
}

void sg_voice_say(const char *text)
{
    if (!s_lock || !text) return;
    char *clean = clean_text(text);
    if (!clean) return;
    uint64_t key = speech_key(clean);
    SDL_LockMutex(s_lock);
    free(s_pending);
    s_pending = NULL;
    s_request++;
    s_request_key = key;
    s_pcm_len = 0;
    s_pos = 0;
    s_pages_known = s_shown_page = s_play_page = 0;
    s_hold = true;                       /* silent until sg_voice_release() */
    s_first_sound_logged = false;
    s_from_memory = *clean && recall(key);
    if (s_from_memory) s_cache_hits++;
    else if (*clean) s_cache_misses++;
    if (*clean && !s_from_memory) {
        s_pending = clean;               /* the voice thread makes it */
        clean = NULL;
        SDL_CondSignal(s_wake);
    }
    SDL_UnlockMutex(s_lock);
    free(clean);
}

void sg_voice_set_preset(int preset)
{
    if (preset < 0 || preset >= sg_voice_preset_count) return;
    if (!s_lock) { s_preset = sg_voice_presets[preset]; s_preset_index = preset; return; }
    SDL_LockMutex(s_lock);
    bool changed = preset != s_preset_index;
    s_preset = sg_voice_presets[preset];
    s_preset_index = preset;
    SDL_UnlockMutex(s_lock);
    if (changed) {
        sg_voice_stop();                 /* the old voice stops; the new one loads with the next message */
        sg_log("voice: narrator voice is now %s", sg_voice_presets[preset].name);
    }
}

void sg_voice_set_volume(int volume, int music)
{
    if (s_lock) SDL_LockMutex(s_lock);
    s_volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    s_music_duck = (music < 0 ? 0 : music > 100 ? 100 : music) / 100.0f;
    if (s_lock) SDL_UnlockMutex(s_lock);
}

void sg_voice_set_pauses(float quote_seconds, float exclaim_seconds)
{
    s_quote_pause = quote_seconds < 0 ? 0 : quote_seconds > 3 ? 3 : quote_seconds;
    s_exclaim_pause = exclaim_seconds < 0 ? 0 : exclaim_seconds > 3 ? 3 : exclaim_seconds;
}

void sg_voice_release(void)
{
    if (!s_lock) return;
    SDL_LockMutex(s_lock);
    s_hold = false;
    s_release_ticks = SDL_GetTicks();
    SDL_UnlockMutex(s_lock);
}

void sg_voice_stop(void)
{
    if (!s_lock) return;
    SDL_LockMutex(s_lock);
    free(s_pending);
    s_pending = NULL;
    s_request++;
    s_pcm_len = 0;
    s_pos = 0;
    s_pages_known = s_shown_page = s_play_page = 0;
    s_hold = false;
    s_first_sound_logged = true;
    SDL_UnlockMutex(s_lock);
}

void sg_voice_page(int page)
{
    if (!s_lock) return;
    SDL_LockMutex(s_lock);
    if (page > s_shown_page) s_shown_page = page;
    SDL_UnlockMutex(s_lock);
}

/* Gentle limiter: below 3/4 of full scale nothing changes; above, the
 * sound is squeezed towards full scale instead of being cut off. */
static int16_t soft_limit(float x)
{
    const float knee = 24576.0f, room = 32767.0f - 24576.0f;
    float a = x < 0 ? -x : x;
    if (a > knee) a = knee + room * (a - knee) / (a - knee + room);
    return (int16_t)(x < 0 ? -a : a);
}

void sg_voice_mix(int16_t *pcm, size_t count, unsigned rate)
{
    if (!s_lock || !rate) return;
    SDL_LockMutex(s_lock);
    /* Playing Kokoro's samples a little slower than they were made lowers
     * the voice (pitch 0.85 = 15 % deeper and 15 % slower). */
    double step = s_pcm_rate * (double)s_preset.pitch / rate;
    /* The player turned the page: go to its start (once it is made). */
    bool waiting = s_hold;
    if (s_play_page < s_shown_page) {
        if (s_shown_page >= s_pages_known) waiting = true;     /* not made yet */
        else {
            s_play_page = s_shown_page;
            if (s_pos < (double)s_page_start[s_play_page]) s_pos = (double)s_page_start[s_play_page];
        }
    }
    /* ...and stop at the end of the page being shown. */
    size_t end = s_play_page + 1 < s_pages_known ? s_page_start[s_play_page + 1] : s_pcm_len;
    float gain = 0.95f / s_peak * 32767.0f * (float)s_volume / 100.0f;
    /* While the voice talks the game's own sound is turned down
     * (MusicWhileSpeaking), fading over about a fifth of a second. */
    float fade = 5.0f / (float)rate;
    bool first_now = false;
    for (size_t i = 0; i < count; ++i) {
        size_t k = (size_t)s_pos;
        bool talking = !waiting && k + 1 < end;   /* false: page end, caught up, or nothing to say */
        float target = talking ? s_music_duck : 1.0f;
        if (s_duck < target) { s_duck += fade; if (s_duck > target) s_duck = target; }
        else if (s_duck > target) { s_duck -= fade; if (s_duck < target) s_duck = target; }
        float x = (float)pcm[i] * s_duck;
        if (talking) {
            if (!s_first_sound_logged) { s_first_sound_logged = true; first_now = true; }
            float f = (float)(s_pos - (double)k);
            x += (s_pcm[k] + (s_pcm[k + 1] - s_pcm[k]) * f) * gain;
            s_pos += step;
        }
        pcm[i] = soft_limit(x);
    }
    Uint32 delay = SDL_GetTicks() - s_release_ticks;
    bool memory = s_from_memory;
    unsigned hits = s_cache_hits, total = s_cache_hits + s_cache_misses;
    double kept = s_cache_samples / 24000.0;
    SDL_UnlockMutex(s_lock);
    if (first_now)
        sg_log("voice: first sound %u ms after the box appeared (%s; remembered %u of %u messages, %.0f s kept)",
               (unsigned)delay, memory ? "from memory" : "newly made", hits, total, kept);
}

#else   /* built without sherpa-onnx */

bool sg_voice_built_in(void) { return false; }
void sg_voice_start(int preset, const char *model_dir, int volume, int music)
{
    (void)preset; (void)model_dir; (void)volume; (void)music;
    sg_log("voice: this build has no voice (sherpa-onnx was not found when it was built)");
}
void sg_voice_say(const char *text) { (void)text; }
void sg_voice_page(int page) { (void)page; }
void sg_voice_release(void) {}
void sg_voice_set_preset(int preset) { (void)preset; }
void sg_voice_set_volume(int volume, int music) { (void)volume; (void)music; }
void sg_voice_set_pauses(float quote_seconds, float exclaim_seconds) { (void)quote_seconds; (void)exclaim_seconds; }
void sg_voice_stop(void) {}
void sg_voice_mix(int16_t *pcm, size_t count, unsigned rate) { (void)pcm; (void)count; (void)rate; }

#endif
