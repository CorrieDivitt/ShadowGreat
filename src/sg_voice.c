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
 * The voice is an optional add-on (the "voice package"): sherpa-onnx's DLL
 * and the Kokoro model, in the voice folder beside the .exe. The DLL is
 * loaded while the game runs (load_library); without it narration is just
 * unavailable and the rest of the game is unaffected.
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

/* The three voices, chosen by the user from samples on 2026-11-08
 * (p10-narrators). Dungeon Master is the original narrator; Old Man and
 * Woman replaced the earlier Grim and Plain. */
#if defined(SG_ROM_REGION_GERMANY)
const SgVoicePreset sg_voice_presets[] = {
    /* name              sid blend  mix   speed  pitch  pause */
    { "Dungeon Master",  27,  -1,  0.0f,  1.15f, 0.90f, 0.5f },  /* bm_lewis - deep male narrator */
    { "Alter Mann",      19,  -1,  0.0f,  1.05f, 0.95f, 0.6f },  /* am_santa - mature male */
    { "Erzählerin",       6,  -1,  0.0f,  1.00f, 1.00f, 0.6f },  /* af_nicole - female */
};
#elif defined(SG_ROM_REGION_SWEDEN)
const SgVoicePreset sg_voice_presets[] = {
    /* name              sid blend  mix   speed  pitch  pause */
    { "Dungeon Master",  27,  -1,  0.0f,  1.12f, 0.95f, 0.5f },  /* bm_lewis - authentic original Dungeon Master */
    { "Gammal man",      19,  -1,  0.0f,  1.05f, 0.95f, 0.6f },  /* am_santa - mature male */
    { "Kvinna",           6,  -1,  0.0f,  1.00f, 1.00f, 0.6f },  /* af_nicole - female */
};
#elif defined(SG_ROM_REGION_FRANCE)
const SgVoicePreset sg_voice_presets[] = {
    /* name              sid blend  mix   speed  pitch  pause */
    { "Maitre du Donjon",27,  -1,  0.0f,  1.10f, 0.90f, 0.5f },  /* bm_lewis - deep male narrator */
    { "Vieil Homme",     19,  -1,  0.0f,  1.05f, 0.95f, 0.6f },  /* am_santa - mature male */
    { "Narratrice",      30,  -1,  0.0f,  1.00f, 1.00f, 0.6f },  /* ff_siwis - female */
};
#else
const SgVoicePreset sg_voice_presets[] = {
    /* name              sid blend  mix   speed  pitch  pause */
    { "Dungeon Master",  27,  -1,  0.0f,  1.22f, 0.85f, 0.5f },  /* bm_lewis, deeper  */
    { "Old Man",         19,  -1,  0.0f,  1.12f, 0.95f, 0.6f },  /* am_santa, a little deeper */
    { "Woman",            6,  -1,  0.0f,  1.00f, 1.00f, 0.6f },  /* af_nicole as is   */
};
#endif
const int sg_voice_preset_count = (int)(sizeof sg_voice_presets / sizeof sg_voice_presets[0]);

#if defined(SG_HAVE_VOICE) && defined(CYC_WITH_SDL)

#include <SDL.h>
#include "sherpa-onnx/c-api/c-api.h"   /* third_party/sherpa-onnx (v2.0.1, Apache-2.0) */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* ---- sherpa-onnx, loaded from the voice package while the game runs ---- */
static char s_voice_dir[1024];          /* the voice folder (DLLs, and the model by default) */
static bool s_unavailable;              /* the voice package is missing or broken */
static const SherpaOnnxOfflineTts *(*p_create)(const SherpaOnnxOfflineTtsConfig *);
static void (*p_destroy)(const SherpaOnnxOfflineTts *);
static int32_t (*p_sample_rate)(const SherpaOnnxOfflineTts *);
static const SherpaOnnxGeneratedAudio *(*p_generate)(const SherpaOnnxOfflineTts *, const char *,
        const SherpaOnnxGenerationConfig *, SherpaOnnxGeneratedAudioProgressCallbackWithArg, void *);
static void (*p_free_audio)(const SherpaOnnxGeneratedAudio *);

static bool load_library(void)
{
    static bool tried, ok;
    if (tried) return ok;
    tried = true;
#if defined(_WIN32)
    char path[1200];
    snprintf(path, sizeof path, "%s\\sherpa-onnx-c-api.dll", s_voice_dir);
    /* ALTERED_SEARCH_PATH: its own DLLs (onnxruntime.dll) are found beside it. */
    HMODULE lib = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#define SG_SYM(name) GetProcAddress(lib, name)
#else
    char path[1200];
    snprintf(path, sizeof path, "%s/libsherpa-onnx-c-api.so", s_voice_dir);
    void *lib = SDL_LoadObject(path);
#define SG_SYM(name) SDL_LoadFunction(lib, name)
#endif
    if (!lib) {
        sg_log("voice: the voice package is not installed (no %s); narration is off", path);
        return false;
    }
    p_create      = (const SherpaOnnxOfflineTts *(*)(const SherpaOnnxOfflineTtsConfig *))(void (*)(void))SG_SYM("SherpaOnnxCreateOfflineTts");
    p_destroy     = (void (*)(const SherpaOnnxOfflineTts *))(void (*)(void))SG_SYM("SherpaOnnxDestroyOfflineTts");
    p_sample_rate = (int32_t (*)(const SherpaOnnxOfflineTts *))(void (*)(void))SG_SYM("SherpaOnnxOfflineTtsSampleRate");
    p_generate    = (const SherpaOnnxGeneratedAudio *(*)(const SherpaOnnxOfflineTts *, const char *,
                        const SherpaOnnxGenerationConfig *, SherpaOnnxGeneratedAudioProgressCallbackWithArg,
                        void *))(void (*)(void))SG_SYM("SherpaOnnxOfflineTtsGenerateWithConfig");
    p_free_audio  = (void (*)(const SherpaOnnxGeneratedAudio *))(void (*)(void))SG_SYM("SherpaOnnxDestroyOfflineTtsGeneratedAudio");
#undef SG_SYM
    ok = p_create && p_destroy && p_sample_rate && p_generate && p_free_audio;
    if (!ok) sg_log("voice: %s is not the expected sherpa-onnx version (2.0.1); narration is off", path);
    return ok;
}
#define SherpaOnnxCreateOfflineTts                p_create
#define SherpaOnnxDestroyOfflineTts               p_destroy
#define SherpaOnnxOfflineTtsSampleRate            p_sample_rate
#define SherpaOnnxOfflineTtsGenerateWithConfig    p_generate
#define SherpaOnnxDestroyOfflineTtsGeneratedAudio p_free_audio

/* false once we know the voice package is missing (the menu greys its rows) */
bool sg_voice_built_in(void) { return !s_unavailable; }

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
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\'' || (unsigned char)c >= 0x80;
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
        for (size_t i = s; i < n; ++i) {
            if (w[i] >= 'A' && w[i] <= 'Z') {
                w[i] = (char)(w[i] - 'A' + 'a');
            } else if ((unsigned char)w[i] == 0xC3 && i + 1 < n) {
                if ((unsigned char)w[i + 1] == 0x84) w[i + 1] = (char)0xA4;
                else if ((unsigned char)w[i + 1] == 0x85) w[i + 1] = (char)0xA5;
                else if ((unsigned char)w[i + 1] == 0x96) w[i + 1] = (char)0xB6;
                else if ((unsigned char)w[i + 1] == 0x9C) w[i + 1] = (char)0xBC;
            }
        }
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
#if defined(SG_ROM_REGION_GERMANY)
static const struct { const char *from, *to; } PHRASE_FIXES[] = {
    { "ABERDIE",       "ABER DIE" },
    { "ABERNICHTS",    "ABER NICHTS" },
    { "ANDAS",         "AN DAS" },
    { "ANDERENSEITE",  "ANDEREN SEITE" },
    { "ARTZAUBERWORT", "ART ZAUBERWORT" },
    { "AUGENZUAMMEN",  "AUGEN ZUSAMMEN" },
    { "AUSSOLIDEM",    "AUS SOLIDEM" },
    { "BESSERVERBRINGEN", "BESSER VERBRINGEN" },
    { "BRÜCKELEICHT",  "BRÜCKE LEICHT" },
    { "DALÖST",        "DA LÖST" },
    { "DASBILD",       "DAS BILD" },
    { "DASGROLLEN",    "DAS GROLLEN" },
    { "DASLICHT",      "DAS LICHT" },
    { "DAßDU",         "DAß DU" },
    { "DEINEKRÄFTE",   "DEINE KRÄFTE" },
    { "DEINEMGEWICHT", "DEINEM GEWICHT" },
    { "DEMFEIND",      "DEM FEIND" },
    { "DENWEG",        "DEN WEG" },
    { "DERFACKEL",     "DER FACKEL" },
    { "DERSIEG",       "DER SIEG" },
    { "DERZWÖLF",      "DER ZWÖLF" },
    { "DICHDEN",       "DICH DEN" },
    { "DICHSCHON",     "DICH SCHON" },
    { "DIEHAND",       "DIE HAND" },
    { "DIESICH",       "DIE SICH" },
    { "DIRAUF",        "DIR AUF" },
    { "DIRDEN",        "DIR DEN" },
    { "DUDAS",         "DU DAS" },
    { "DUDIE",         "DU DIE" },
    { "DURCHSCHLAGENBETHMOTH", "DURCHSCHLAGEN BETHMOTH" },
    { "DÄMONVERSPERRT","DÄMON VERSPERRT" },
    { "EINENDURCHLÖCHERTEN", "EINEN DURCHLÖCHERTEN" },
    { "EINFACHWUNDERBAR", "EINFACH WUNDERBAR" },
    { "EINHEXENBESEN", "EIN HEXENBESEN" },
    { "EINKLEINES",    "EIN KLEINES" },
    { "EINMAGISCHES",  "EIN MAGISCHES" },
    { "ERHÄLST",       "ERHÄLTST" },
    { "ESINS",         "ES INS" },
    { "ETWASFEHLT",    "ETWAS FEHLT" },
    { "FACKELHÄLST",   "FACKEL HÄLTST" },
    { "FACKELHÄLTST",  "FACKEL HÄLTST" },
    { "FÜRCHTESR",     "FÜRCHTEST" },
    { "GEHTDA",        "GEHT DA" },
    { "GERADZU",       "GERADEZU" },
    { "GERÄUSCHZERPLATZEN", "GERÄUSCH ZERPLATZEN" },
    { "GEWISSERMAßENDIE", "GEWISSERMAßEN DIE" },
    { "GLÄNZTALS",     "GLÄNZT ALS" },
    { "GRABWERDEN",    "GRAB WERDEN" },
    { "GÜCKWUNSCH",    "GLÜCKWUNSCH" },
    { "HERSCHT",       "HERRSCHT" },
    { "HERZAN",        "HERZ AN" },
    { "HILFEEE",       "HILFE" },
    { "HOLZFÜR",       "HOLZ FÜR" },
    { "HÖHLESTEIGT",   "HÖHLE STEIGT" },
    { "HÄLST",         "HÄLTST" },
    { "IRGENWIE",      "IRGENDWIE" },
    { "ISTEIN",        "IST EIN" },
    { "KLEINESZAHNRAD","KLEINES ZAHNRAD" },
    { "LICHTSTRAHLSCHEINT", "LICHTSTRAHL SCHEINT" },
    { "LUFTAUS",       "LUFT AUS" },
    { "MAULWEIT",      "MAUL WEIT" },
    { "MITEINEM",      "MIT EINEM" },
    { "MITUNGLAUBLICHER", "MIT UNGLAUBLICHER" },
    { "NAHEAN",        "NAHE AN" },
    { "NICHTSMEHR",    "NICHTS MEHR" },
    { "NICHTUND",      "NICHT UND" },
    { "PLATINHORNHERAUFZUBESCHÖREN", "PLATINHORN HERAUFZUBESCHWÖREN" },
    { "PLÖZLICH",      "PLÖTZLICH" },
    { "SCHRECKNICHT",  "SCHRECK NICHT" },
    { "SECHSSTRAHLIGERSTERN", "SECHSSTRAHLIGER STERN" },
    { "SICHÜBER",      "SICH ÜBER" },
    { "SIEHTNUR",      "SIEHT NUR" },
    { "SIEHTZIEMLICH", "SIEHT ZIEMLICH" },
    { "SOSCHMAL",      "SO SCHMAL" },
    { "SPRINGENDICH",  "SPRINGEN DICH" },
    { "TALISMANN",     "TALISMAN" },
    { "TROPT",         "TROPFT" },
    { "UNDBILDET",     "UND BILDET" },
    { "UNDVERBIRGT",   "UND VERBIRGT" },
    { "UNTERNEBMEN",   "UNTERNEHMEN" },
    { "VERGITTERTEFENSTER", "VERGITTERTE FENSTER" },
    { "VERWEHTIM",     "VERWEHT IM" },
    { "VORDEINEN",     "VOR DEINEN" },
    { "WIEDEREINEN",   "WIEDER EINEN" },
    { "WIEEINE",       "WIE EINE" },
    { "WIEVERSTEINERT","WIE VERSTEINERT" },
    { "WINDETSICH",    "WINDET SICH" },
    { "WIRSTAUCH",     "WIRST AUCH" },
    { "ZAUBERFACKELNACH", "ZAUBERFACKEL NACH" },
    { "ZAUBERHAFTEATMOSPHÄRE", "ZAUBERHAFTE ATMOSPHÄRE" },
    { "ZAUBERSPRÜCHEGEGEN", "ZAUBERSPRÜCHE GEGEN" },
    { "ZOMBI",         "ZOMBIE" },
    { "ZUDIR",         "ZU DIR" },
    { "ZUHÄLST",       "ZU HÄLTST" },
    { "ZUHÄLTST",      "ZU HÄLTST" },
    { "ZUVIEL",        "ZU VIEL" },
    { "UNHEILSTIFTERIST", "UNHEILSTIFTER IST" },
    { "NÄMLICHDU",     "NÄMLICH DU" },
    { "100 BEINE",     "HUNDERT BEINE" },
};
#elif defined(SG_ROM_REGION_SWEDEN)
static const struct { const char *from, *to; } PHRASE_FIXES[] = {
    { "ATTDE",              "ATT DE" },
    { "ATTPIRRA",           "ATT PIRRA" },
    { "ATTRÖRA",            "ATT RÖRA" },
    { "ATTVARA",            "ATT VARA" },
    { "ARVTAGARENTILL",     "ARVTAGAREN TILL" },
    { "AVUNDERBAR",         "AV UNDERBAR" },
    { "AVVÅLDSAMMA",        "AV VÅLDSAMMA" },
    { "BLIDIN",             "BLI DIN" },
    { "DETDU",              "DET DU" },
    { "DIGATT",             "DIG ATT" },
    { "DIGPÅ",              "DIG PÅ" },
    { "DUBRUKAR",           "DU BRUKAR" },
    { "DUDÖR",              "DU DÖR" },
    { "DUKOMMER",           "DU KOMMER" },
    { "DUSKALL",            "DU SKALL" },
    { "DUUTAN",             "DU UTAN" },
    { "DÖRINOM",            "DÖR INOM" },
    { "ELDENSÅ",            "ELDEN SÅ" },
    { "ENLITEN",            "EN LITEN" },
    { "ETTRINGFORMAT",       "ETT RINGFORMAT" },
    { "ETTÖGONBLICK",        "ETT ÖGONBLICK" },
    { "FRAMFÖRDIG",         "FRAMFÖR DIG" },
    { "FRÅNDE",             "FRÅN DE" },
    { "FRÅNHANS",           "FRÅN HANS" },
    { "FRÅNPLATTFORMEN",     "FRÅN PLATTFORMEN" },
    { "FÅKALLA",            "FÅ KALLA" },
    { "FÖRALLTID",          "FÖR ALLTID" },
    { "GLADAÖVER",          "GLADA ÖVER" },
    { "HETTAATT",           "HETTA ATT" },
    { "HÄRÄR",              "HÄR ÄR" },
    { "HÖLLNÅGOT",          "HÖLL NÅGOT" },
    { "IASKAN",             "I ASKAN" },
    { "ISTEN",              "I STEN" },
    { "KLIPPANOCH",         "KLIPPAN OCH" },
    { "KUNGLIGASTADEN",     "KUNGLIGA STADEN" },
    { "KÄNNERLUKTEN",       "KÄNNER LUKTEN" },
    { "LITENTRONSAL",       "LITEN TRONSAL" },
    { "LJUSETMYCKET",       "LJUSET MYCKET" },
    { "LUNGORNAFYLLS",      "LUNGORNA FYLLS" },
    { "MENDEN",             "MEN DEN" },
    { "OCHDÖLJER",          "OCH DÖLJER" },
    { "OCHSVÄR",            "OCH SVÄR" },
    { "OCHTRÄNGER",         "OCH TRÄNGER" },
    { "OSSALLA",            "OSS ALLA" },
    { "PILPÅ",              "PIL PÅ" },
    { "PLÖTSLIGTPÅ",        "PLÖTSLIGT PÅ" },
    { "RÄDDATOCH",          "RÄDDAT OCH" },
    { "SADEDET",            "SADE DET" },
    { "SIGILLÄR",           "SIGILL ÄR" },
    { "SIGPLÖTSLIGT",       "SIG PLÖTSLIGT" },
    { "SIGSIGILLET",        "SIG SIGILLET" },
    { "SJUNKERNER",         "SJUNKER NER" },
    { "SKRIKBLI",           "SKRIK BLI" },
    { "SLÅRSNABBT",         "SLÅR SNABBT" },
    { "SMÄRTAOCH",          "SMÄRTA OCH" },
    { "SOMBEHÖVS",          "SOM BEHÖVS" },
    { "SOMHÅLLS",           "SOM HÅLLS" },
    { "SPEGELNFINNS",       "SPEGELN FINNS" },
    { "SPEGELNFLYGER",      "SPEGELN FLYGER" },
    { "SPEGLAROCH",         "SPEGLAR OCH" },
    { "STRIDSVRÅLOCH",      "STRIDSVRÅL OCH" },
    { "STÅRETT",            "STÅR ETT" },
    { "SVETTENRINNER",      "SVETTEN RINNER" },
    { "TILLKNÄNA",          "TILL KNÄNA" },
    { "TILLSIST",           "TILL SIST" },
    { "UPPDIG",             "UPP DIG" },
    { "VADDU",              "VAD DU" },
    { "VOREINTE",           "VORE INTE" },
    { "VÄLDIGAGRANITBLOCK", "VÄLDIGA GRANITBLOCK" },
    { "ÄRDET",              "ÄR DET" },
    { "ÄRHAN",              "ÄR HAN" },
    { "ÄRVÄRDIG",           "ÄR VÄRDIG" },
    { "ÖDESTÅR",            "ÖDE STÅR" },
    { "ALLDLES",            "ALLDELES" },
    { "DOKUM.",             "DOKUMENT" },
    { "DOKUM",              "DOKUMENT" },
    { "ELDGAFFL",           "ELDGAFFEL" },
    { "SKRIVBRD",           "SKRIVBORD" },
    { "TROLLFORM",          "TROLLFORMEL" },
    { "VARULVN",            "VARULVEN" },
    { "KOPPMYNT",           "KOPPARMYNT" },
    { "STORMYNT",           "STORT MYNT" },
    { "EKARI",              "EKAR I" },
    { "100",                "HUNDRA" },
};
#else
static const struct { const char *from, *to; } PHRASE_FIXES[] = {
    /* "winds" as in a winding path (sg-names: wynds). The game also says
     * "THE SWIRLING WINDS", which stays as it is. Found by scanning every
     * WIND in the game's text, 2026-11-08. */
    { "PATH WINDS",    "PATH WYNDS" },      /* the prophecy: THE PATH WINDS ROUND */
    { "PASSAGE WINDS", "PASSAGE WYNDS" },   /* THE STONE PASSAGE WINDS TO AN UNSEEN END */
    { "SCRIPT WINDS",  "SCRIPT WYNDS" },    /* ...SCRIPT WINDS ITS WAY... */
    /* Found by the message sweep (every message in the game), 2026-11-08.
     * Item names the game squeezes to fit its word list: */
    { "COPPCOIN",      "COPPER COIN" },
    { "GOLDCOIN",      "GOLD COIN" },
    { "HRSESHOE",      "HORSESHOE" },
    { "TESTTUBE",      "TEST TUBE" },
    { "WHITEGEM",      "WHITE GEM" },
    /* The game's own typos: */
    { "INTHE",         "IN THE" },          /* YOU PUT THE ... INTHE FIRE */
    { "COULD FOLL",    "COULD FOOL" },      /* ...YOU NEVER COULD FOLL ME! */
    /* The voice can't say digits; the game's only number: */
    { "100 LEGS",      "ONE HUNDRED LEGS" },
    /* Said two ways; the word lists only have the other one (sg-names: redd, livv): */
    { "YOU'VE READ",   "YOU'VE REDD" },     /* ...YOU'VE READ THE SCROLL */
    { "WAS READ",      "WAS REDD" },        /* THE ... WAS READ */
    { "NOT LIVE",      "NOT LIVV" },        /* TARKUS WILL NOT LIVE TO SEE THE RISING SUN */
};
#endif
#define PHRASE_FIXES_VERSION 4         /* bump when the list changes (memory key) */

static bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\'' || (unsigned char)c >= 0x80; }

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
    /* Lowercase UTF-8 umlauts for TTS lexicon matching */
    for (size_t i = 0; i + 1 < o; ++i) {
        if ((unsigned char)out[i] == 0xC3) {
            if ((unsigned char)out[i + 1] == 0x84) out[i + 1] = (char)0xA4;      /* Ä -> ä */
            else if ((unsigned char)out[i + 1] == 0x85) out[i + 1] = (char)0xA5; /* Å -> å */
            else if ((unsigned char)out[i + 1] == 0x96) out[i + 1] = (char)0xB6; /* Ö -> ö */
            else if ((unsigned char)out[i + 1] == 0x9C) out[i + 1] = (char)0xBC; /* Ü -> ü */
        }
    }
    free(in);
    return out;
}

/* ---- a blended voice (for a preset with blend_sid >= 0; none uses one now) ----
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
    if (!load_library()) { s_unavailable = true; return NULL; }
    if (!file_exists(model) || !file_exists(voices) || !file_exists(tokens)) {
        sg_log("voice: the voice package has no Kokoro model in \"%s\"; narration is off", s_model_dir);
        s_unavailable = true;
        return NULL;
    }
#if defined(SG_ROM_REGION_GERMANY)
    char de_lex[1200], de_names[1200];
    join(de_lex, sizeof de_lex, s_base, "lexicon-de.txt");
    if (!file_exists(de_lex)) join(de_lex, sizeof de_lex, s_voice_dir, "/lexicon-de.txt");
    join(de_names, sizeof de_names, s_base, "sg-german.txt");
    if (!file_exists(de_names)) join(de_names, sizeof de_names, s_voice_dir, "/sg-german.txt");

    snprintf(lexicons, sizeof lexicons, "%s%s%s",
             file_exists(de_lex) ? de_lex : "",
             (file_exists(de_lex) && file_exists(de_names)) ? "," : "",
             file_exists(de_names) ? de_names : "");
    if (!file_exists(de_lex)) sg_log("voice: lexicon-de.txt is missing in %s; German voice may skip words", s_voice_dir);
#elif defined(SG_ROM_REGION_SWEDEN)
    char sv_lex[1200], sv_names[1200];
    join(sv_lex, sizeof sv_lex, s_base, "lexicon-sv.txt");
    if (!file_exists(sv_lex)) join(sv_lex, sizeof sv_lex, s_voice_dir, "/lexicon-sv.txt");
    join(sv_names, sizeof sv_names, s_base, "sg-swedish.txt");
    if (!file_exists(sv_names)) join(sv_names, sizeof sv_names, s_voice_dir, "/sg-swedish.txt");

    snprintf(lexicons, sizeof lexicons, "%s%s%s",
             file_exists(sv_lex) ? sv_lex : "",
             (file_exists(sv_lex) && file_exists(sv_names)) ? "," : "",
             file_exists(sv_names) ? sv_names : "");
    if (!file_exists(sv_lex)) sg_log("voice: lexicon-sv.txt is missing in %s; Swedish voice may skip words", s_voice_dir);
#elif defined(SG_ROM_REGION_FRANCE)
    char fr_lex[1200], fr_names[1200];
    join(fr_lex, sizeof fr_lex, s_base, "lexicon-fr.txt");
    if (!file_exists(fr_lex)) join(fr_lex, sizeof fr_lex, s_voice_dir, "/lexicon-fr.txt");
    join(fr_names, sizeof fr_names, s_base, "sg-french.txt");
    if (!file_exists(fr_names)) join(fr_names, sizeof fr_names, s_voice_dir, "/sg-french.txt");

    snprintf(lexicons, sizeof lexicons, "%s%s%s",
             file_exists(fr_lex) ? fr_lex : "",
             (file_exists(fr_lex) && file_exists(fr_names)) ? "," : "",
             file_exists(fr_names) ? fr_names : "");
    if (!file_exists(fr_lex)) sg_log("voice: lexicon-fr.txt is missing in %s; French voice may skip words", s_voice_dir);
#else
    /* British first, US fills gaps, ours last: the LAST list wins. */
    snprintf(lexicons, sizeof lexicons, "%s/lexicon-us-en.txt,%s/lexicon-gb-en.txt%s%s",
             s_model_dir, s_model_dir, file_exists(ours) ? "," : "", file_exists(ours) ? ours : "");
    if (!file_exists(ours)) sg_log("voice: sg-names.txt is missing beside the .exe; names will be skipped");
#endif

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
#if defined(SG_ROM_REGION_GERMANY)
        if (file_exists(de_lex) && (n = load_word_list(de_lex)) > 0) total += n;
        if (file_exists(de_names) && (n = load_word_list(de_names)) > 0) total += n;
#elif defined(SG_ROM_REGION_SWEDEN)
        if (file_exists(sv_lex) && (n = load_word_list(sv_lex)) > 0) total += n;
        if (file_exists(sv_names) && (n = load_word_list(sv_names)) > 0) total += n;
#elif defined(SG_ROM_REGION_FRANCE)
        if (file_exists(fr_lex) && (n = load_word_list(fr_lex)) > 0) total += n;
        if (file_exists(fr_names) && (n = load_word_list(fr_names)) > 0) total += n;
#else
        join(p, sizeof p, s_model_dir, "/lexicon-us-en.txt"); if ((n = load_word_list(p)) > 0) total += n;
        join(p, sizeof p, s_model_dir, "/lexicon-gb-en.txt"); if ((n = load_word_list(p)) > 0) total += n;
        if ((n = load_word_list(ours)) > 0) total += n;
#endif
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
                for (const char *c = piece; *c; ++c) if (word_char(*c) && *c != '\'') words = true;
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
                    if (word_char(*c) && *c != '\'') more = true;
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

/* A folder setting: relative = beside the .exe; no trailing slash. */
static void resolve_dir(char *out, size_t n, const char *dir)
{
    if (is_absolute(dir)) snprintf(out, n, "%s", dir);
    else snprintf(out, n, "%s%s", s_base, dir);
    size_t len = strlen(out);
    while (len && (out[len - 1] == '/' || out[len - 1] == '\\')) out[--len] = 0;
}

void sg_voice_start(int preset, const char *voice_dir, const char *model_dir, int volume, int music)
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
    resolve_dir(s_voice_dir, sizeof s_voice_dir, voice_dir && *voice_dir ? voice_dir : "voice");
    if (model_dir && *model_dir) resolve_dir(s_model_dir, sizeof s_model_dir, model_dir);
    else snprintf(s_model_dir, sizeof s_model_dir, "%s/kokoro-multi-lang-v1_0", s_voice_dir);

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

static float clamp_tuning(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void sg_voice_set_tuning(float speed, float depth, float pause)
{
    if (s_lock) SDL_LockMutex(s_lock);
    s_preset.speed = clamp_tuning(speed, 0.5f, 1.5f);
    s_preset.pitch = clamp_tuning(depth, 0.5f, 1.5f);
    s_preset.pause = clamp_tuning(pause, 0.0f, 2.0f);
    if (s_lock) SDL_UnlockMutex(s_lock);
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
void sg_voice_start(int preset, const char *voice_dir, const char *model_dir, int volume, int music)
{
    (void)preset; (void)voice_dir; (void)model_dir; (void)volume; (void)music;
    sg_log("voice: this build has no voice (built without a window)");
}
void sg_voice_say(const char *text) { (void)text; }
void sg_voice_page(int page) { (void)page; }
void sg_voice_release(void) {}
void sg_voice_set_preset(int preset) { (void)preset; }
void sg_voice_set_volume(int volume, int music) { (void)volume; (void)music; }
void sg_voice_set_tuning(float speed, float depth, float pause) { (void)speed; (void)depth; (void)pause; }
void sg_voice_set_pauses(float quote_seconds, float exclaim_seconds) { (void)quote_seconds; (void)exclaim_seconds; }
void sg_voice_stop(void) {}
void sg_voice_mix(int16_t *pcm, size_t count, unsigned rate) { (void)pcm; (void)count; (void)rate; }

#endif
