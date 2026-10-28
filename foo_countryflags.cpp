// foo_countryflags - exposes country flag fields to title formatting.
//
// Five independently configurable country sources. Each source has a configurable suffix; field names are countryflag_<suffix>_emoji, countryflag_<suffix>_path, etc. The required countryflag_ prefix is always present.
// Source 1 fields: %countryflag_emoji%, %countryflag_path%, %countryflag_name%,
//                  %countryflag_iso2%, %countryflag_iso3%
// Source 2 fields: %countryflag_artist_emoji%, %countryflag_artist_path%,
//                  %countryflag_artist_name%, %countryflag_artist_iso2%, %countryflag_artist_iso3%
// Source 3 fields default suffix: 3 (e.g. %countryflag_3_emoji%).
// Source 4 fields default suffix: 4 (e.g. %countryflag_4_emoji%).
// Source 5 fields default suffix: 5 (e.g. %countryflag_5_emoji%).
// Defaults: source 1 = %country%, source 2 = $if2(%artistcountry%,%countryflag_lookup%); sources 3-5 empty.
// Extra field: %countryflag_lookup% = ISO2 code(s) of the artist found in artist_countries.json
//   (next to the DLL; flat JSON {"artist name": "CC"}, case-insensitive, reloaded when changed).
//   For the playing stream, the dynamic (ICY) artist is searched first.
//   The file location can be set in preferences (empty = next to the DLL).
//   A second table, worldMap.json ([{"artist": "...", "val": [..., "Country"]}]; the last
//   item of val is the country), is searched for artists missing from the first.
// Preferences > Tools > Country Flags.
// The script must not use the %countryflag_*% fields themselves (re-entrant calls
// are ignored to prevent infinite recursion).
//
// Data file: countries.txt next to the DLL, lines of ISO2|ISO3|Name
// Built in (no data-file entry needed): XE/EU = Europe, XW = Worldwide.
//   XE is folded into EU; XW emits a globe emoji and looks for flags\xw.png.
// Flag images: flags\gr.png, flags\us.png, ... next to the DLL (or custom folder).
//
// Build: VS2022, x64, C++17, link foobar2000_SDK, foobar2000_component_client, pfc.
// NOTE: keep this file pure ASCII (use \u / \x escapes). MSVC reads BOM-less
// sources in the ANSI code page unless /utf-8 is set, which garbles literals.

#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/helpers/helpers.h>
#include <foobar2000/SDK/coreDarkMode.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <windows.h>
#include <uxtheme.h>
#include <shobjidl.h>
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")

DECLARE_COMPONENT_VERSION("Country Flags", "0.1.3",
    "Maps country codes/names in tags to flags for title formatting.");
VALIDATE_COMPONENT_FILENAME("foo_countryflags.dll");

namespace {

    namespace fs = std::filesystem;

    const GUID guid_branch = { 0x916ca10, 0x4e9f, 0x45ca, { 0xad, 0x87, 0xc5, 0x4f, 0x1e, 0xcb, 0x8f, 0x46 } };
    const GUID guid_source1_script = { 0x7f6a1021, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x01 } };
    const GUID guid_source2_script = { 0x7f6a1022, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x02 } };
    const GUID guid_source3_script = { 0x7f6a1023, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x03 } };
    const GUID guid_source4_script = { 0x7f6a1024, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x04 } };
    const GUID guid_source5_script = { 0x7f6a1025, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x05 } };
    const GUID guid_artist_file = { 0x3c1d8e47, 0x6a52, 0x4b90, { 0xa7, 0x0e, 0x58, 0xd4, 0x92, 0x1f, 0x6b, 0xc3 } };
    const GUID guid_artist_file2 = { 0x3c1d8e48, 0x6a52, 0x4b90, { 0xa7, 0x0e, 0x58, 0xd4, 0x92, 0x1f, 0x6b, 0xc4 } };
    const GUID guid_dir = { 0xb46e52be, 0x3476, 0x45b5, { 0x98, 0x4f, 0xff, 0x11, 0xbd, 0x1f, 0x98, 0x8b } };
    const GUID guid_prefix1 = { 0x7f6a1031, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x11 } };
    const GUID guid_prefix2 = { 0x7f6a1032, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x12 } };
    const GUID guid_prefix3 = { 0x7f6a1033, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x13 } };
    const GUID guid_prefix4 = { 0x7f6a1034, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x14 } };
    const GUID guid_prefix5 = { 0x7f6a1035, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x15 } };


    // Settings are now exposed on a dedicated Preferences page (Tools > Country Flags),
    // not in Advanced Preferences. Reuse the existing GUIDs where possible.
    cfg_string g_cfgSource1(guid_source1_script, "%country%");
    cfg_string g_cfgSource2(guid_source2_script, "$if2(%artistcountry%,%countryflag_lookup%)");
    cfg_string g_cfgSource3(guid_source3_script, "");
    cfg_string g_cfgSource4(guid_source4_script, "");
    cfg_string g_cfgSource5(guid_source5_script, "");
    cfg_string g_cfgDir(guid_dir, "");
    cfg_string g_cfgArtistFile(guid_artist_file, "");
    cfg_string g_cfgArtistFile2(guid_artist_file2, "");
    cfg_string g_cfgPrefix1(guid_prefix1, "main");
    cfg_string g_cfgPrefix2(guid_prefix2, "artist");
    cfg_string g_cfgPrefix3(guid_prefix3, "3");
    cfg_string g_cfgPrefix4(guid_prefix4, "4");
    cfg_string g_cfgPrefix5(guid_prefix5, "5");

    // Forward declaration: fieldPrefix() uses trim() before its definition below.
    std::string trim(const char* s);

    cfg_string* prefixConfig(size_t index) {
        static cfg_string* configs[] = { &g_cfgPrefix1, &g_cfgPrefix2, &g_cfgPrefix3, &g_cfgPrefix4, &g_cfgPrefix5 };
        return configs[index];
    }

    std::string cleanSuffix(std::string suffix, size_t index) {
        suffix.erase(std::remove_if(suffix.begin(), suffix.end(), [](unsigned char c) {
            return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_');
            }), suffix.end());
        while (!suffix.empty() && suffix.front() == '_') suffix.erase(suffix.begin());
        while (!suffix.empty() && suffix.back() == '_') suffix.pop_back();
        // Migrate the previous built-in names to the new defaults.
        if (index == 2 && (suffix == "country3" || suffix == "suffix3")) suffix = "3";
        if (index == 3 && (suffix == "country4" || suffix == "suffix4")) suffix = "4";
        if (index == 4 && (suffix == "country5" || suffix == "suffix5")) suffix = "5";
        if (suffix.empty()) {
            static const char* defaults[] = { "main", "artist", "3", "4", "5" };
            suffix = defaults[index];
        }
        return suffix;
    }

    std::string fieldPrefix(size_t index) {
        // Source 1 preserves the original, unsuffixed public field names.
        // Sources 2-5 use countryflag_<user suffix>_.
        if (index == 0) return "countryflag_";
        const std::string suffix = cleanSuffix(trim(prefixConfig(index)->get()), index);
        return std::string("countryflag_") + suffix + "_";
    }

    // --- helpers --------------------------------------------------------------

    std::string trim(const char* s) {
        std::string r = s ? s : "";
        auto notspace = [](unsigned char c) { return !isspace(c); };
        r.erase(r.begin(), std::find_if(r.begin(), r.end(), notspace));
        r.erase(std::find_if(r.rbegin(), r.rend(), notspace).base(), r.end());
        return r;
    }
    std::string upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
    std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

    fs::path componentDir() {
        static const fs::path p = fs::u8path(core_api::get_my_full_path()).parent_path();
        return p;
    }

    std::string toUtf8(const fs::path& p) {
        pfc::stringcvt::string_utf8_from_wide u(p.wstring().c_str());
        return std::string(u.get_ptr());
    }

    // --- country database -----------------------------------------------------

    struct Db {
        std::unordered_map<std::string, std::string> a2name;   // "GR" -> "Greece"
        std::unordered_map<std::string, std::string> a3to2;    // "GRC" -> "GR"
        std::unordered_map<std::string, std::string> a2to3;    // "GR" -> "GRC"
        std::unordered_map<std::string, std::string> name2to2; // "greece" -> "GR"
    };

    const Db& db() {
        static Db d;
        static std::once_flag once;
        std::call_once(once, [] {
            std::ifstream f(componentDir() / "countries.txt");
            std::string line;
            while (std::getline(f, line)) {
                if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line.erase(0, 3); // BOM
                if (!line.empty() && line.back() == '\r') line.pop_back();
                auto p1 = line.find('|');
                auto p2 = p1 == std::string::npos ? p1 : line.find('|', p1 + 1);
                if (p2 == std::string::npos) continue;
                std::string a2 = upper(line.substr(0, p1));
                std::string a3 = upper(line.substr(p1 + 1, p2 - p1 - 1));
                std::string nm = line.substr(p2 + 1);
                d.a2name[a2] = nm;
                d.a3to2[a3] = a2;
                d.a2to3[a2] = a3;
                d.name2to2[lower(nm)] = a2;
            }
            // Built-in non-ISO entries (only added if countries.txt does not define them).
            // XE is the MusicBrainz code for Europe and is folded into EU in normalize().
            auto addBuiltin = [&](const char* a2, const char* a3, const char* nm) {
                if (d.a2name.count(a2)) return;
                d.a2name[a2] = nm;
                d.a2to3[a2] = a3;
                d.a3to2.emplace(a3, a2);
                d.name2to2.emplace(lower(nm), a2);
            };
            addBuiltin("EU", "EUR", "Europe");
            addBuiltin("XW", "XWW", "Worldwide");

            // common aliases
            const char* aliases[][2] = {
                {"united states of america", "US"}, {"czech republic", "CZ"}, {"turkiye", "TR"},
                {"t\xC3\xBC" "rkiye", "TR"}, {"burma", "MM"}, {"ivory coast", "CI"}, {"cape verde", "CV"},
                {"vatican", "VA"}, {"macedonia", "MK"}, {"swaziland", "SZ"}, {"east timor", "TL"},
                {"russian federation", "RU"}, {"korea", "KR"}, {"great britain", "GB"},
                {"england", "GB"}, {"scotland", "GB"}, {"wales", "GB"}, {"uk", "GB"},
                {"holland", "NL"}, {"hellas", "GR"},
                {"european union", "EU"}, {"world", "XW"}
            };
            for (auto& a : aliases) d.name2to2[a[0]] = a[1];
            });
        return d;
    }

    // Returns upper-case ISO2, or "" if unrecognised.
    std::string normalize(const char* raw) {
        std::string s = trim(raw);
        // MusicBrainz style "[Europe]" / "[Worldwide]".
        if (s.size() >= 2 && s.front() == '[' && s.back() == ']') s = trim(s.substr(1, s.size() - 2).c_str());
        if (s.empty()) return {};
        const Db& d = db();
        std::string up = upper(s);
        if (up == "XE") return "EU";
        if (up.size() == 2 && d.a2name.count(up)) return up;
        if (up.size() == 3) {
            auto it = d.a3to2.find(up);
            if (it != d.a3to2.end()) return it->second;
        }
        auto it = d.name2to2.find(lower(s));
        return it != d.name2to2.end() ? it->second : std::string();
    }

    std::string emojiFlag(const std::string& iso2) {
        // "Worldwide" has no flag: use the globe-with-meridians emoji (U+1F310).
        if (iso2 == "XW") return "\xF0\x9F\x8C\x90";
        // Regional indicator A = U+1F1E6 -> UTF-8 F0 9F 87 A6
        std::string r;
        for (char c : iso2) {
            if (c < 'A' || c > 'Z') return {};
            r += "\xF0\x9F\x87";
            r += (char)(0xA6 + (c - 'A'));
        }
        return r;
    }

    // Resolves the flag image path for an ISO2 code. Results are cached per code, so
    // the filesystem is hit at most once per code instead of once per evaluation.
    // The cache is cleared when the flag folder setting changes, and every entry
    // (found or missing) is re-checked after a short delay, so flags added or
    // removed while running are picked up.
    std::string flagPath(const std::string& iso2) {
        using clock = std::chrono::steady_clock;
        struct Entry { std::string path; clock::time_point checked; };

        static std::mutex m;
        static bool init = false;
        static std::string lastDir;
        static std::unordered_map<std::string, Entry> cache;
        constexpr auto ttl = std::chrono::seconds(30);

        pfc::string8 custom;
        const std::string customDir = trim(g_cfgDir.get());

        std::lock_guard<std::mutex> lock(m);
        if (!init || lastDir != customDir) {
            cache.clear();
            lastDir = customDir;
            init = true;
        }

        const auto now = clock::now();
        auto it = cache.find(iso2);
        if (it != cache.end() && now - it->second.checked < ttl)
            return it->second.path;

        fs::path dir = customDir.empty() ? componentDir() / "flags" : fs::u8path(customDir);

        // Europe is stored as EU; also accept a flag file named xe.png.
        std::vector<std::string> stems{ lower(iso2) };
        if (iso2 == "EU") stems.push_back("xe");

        std::error_code ec;
        std::string result;
        for (const auto& stem : stems) {
            fs::path p = dir / (stem + ".png");
            if (fs::exists(p, ec)) { result = toUtf8(p); break; }
        }
        cache[iso2] = Entry{ result, now };
        return result;
    }

    // --- artist lookup table --------------------------------------------------
    // artist_countries.json (next to the DLL) is a flat JSON object mapping an artist name
    // to a country, e.g. {"abba": "SE", "aftermath": "US-NJ"}. Names are matched
    // case-insensitively; a region suffix ("US-NJ") is ignored and "XE" means Europe.
    // The file is re-read automatically (checked every few seconds) when it changes.

    using ArtistMap = std::unordered_map<std::string, std::string>;

    std::wstring widenUtf8(const std::string& s) {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return {};
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
        return w;
    }

    std::string narrowUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};
        std::string s((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
    }

    // Case-folded (Unicode-aware), trimmed key used for both the table and the names looked up.
    std::string lookupKey(const std::string& s) {
        std::wstring w = widenUtf8(s);
        if (w.empty()) return {};
        std::wstring lowered(w.size(), L'\0');
        const int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w.c_str(), (int)w.size(),
            &lowered[0], (int)lowered.size(), nullptr, nullptr, 0);
        if (n > 0) { lowered.resize((size_t)n); w = lowered; }
        auto isSpace = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xA0; };
        size_t b = 0, e = w.size();
        while (b < e && isSpace(w[b])) ++b;
        while (e > b && isSpace(w[e - 1])) --e;
        return narrowUtf8(w.substr(b, e - b));
    }

    void appendUtf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    }

    // Minimal JSON reader (UTF-8, optional BOM) for the two supported table formats.
    struct JsonCursor {
        const std::string& t;
        size_t i = 0;
        explicit JsonCursor(const std::string& text) : t(text) {
            if (t.size() >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) i = 3;
        }
        void skipWs() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) ++i; }
        bool peek(char c) { skipWs(); return i < t.size() && t[i] == c; }
        bool eat(char c) { if (!peek(c)) return false; ++i; return true; }
        bool hex4(unsigned& v) {
            if (i + 4 > t.size()) return false;
            v = 0;
            for (int k = 0; k < 4; ++k) {
                const char c = t[i + k];
                v <<= 4;
                if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
                else return false;
            }
            i += 4;
            return true;
        }
        bool readString(std::string& str) {
            skipWs();
            if (i >= t.size() || t[i] != '"') return false;
            ++i;
            str.clear();
            while (i < t.size()) {
                const char c = t[i++];
                if (c == '"') return true;
                if (c != '\\') { str += c; continue; }
                if (i >= t.size()) return false;
                const char e = t[i++];
                switch (e) {
                case '"': str += '"'; break;
                case '\\': str += '\\'; break;
                case '/': str += '/'; break;
                case 'b': str += '\b'; break;
                case 'f': str += '\f'; break;
                case 'n': str += '\n'; break;
                case 'r': str += '\r'; break;
                case 't': str += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                        const size_t save = i;
                        i += 2;
                        unsigned lo = 0;
                        if (hex4(lo) && lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else i = save;
                    }
                    appendUtf8(str, cp);
                    break;
                }
                default: return false;
                }
            }
            return false;
        }
        // Skips any JSON value (used for keys we do not care about).
        bool skipValue(int depth = 0) {
            if (depth > 32) return false;
            skipWs();
            if (i >= t.size()) return false;
            const char c = t[i];
            if (c == '"') { std::string tmp; return readString(tmp); }
            if (c == '[' || c == '{') {
                const char close = c == '[' ? ']' : '}';
                ++i;
                if (eat(close)) return true;
                for (;;) {
                    if (c == '{') {
                        std::string key;
                        if (!readString(key) || !eat(':')) return false;
                    }
                    if (!skipValue(depth + 1)) return false;
                    if (eat(',')) continue;
                    return eat(close);
                }
            }
            const size_t start = i;   // number / true / false / null
            while (i < t.size() && t[i] != ',' && t[i] != ']' && t[i] != '}' && t[i] != ' ' && t[i] != '\t' && t[i] != '\r' && t[i] != '\n') ++i;
            return i > start;
        }
    };

    // Format 1: flat object {"artist name": "country", ...}.
    bool parseFlatJsonObject(const std::string& text, std::vector<std::pair<std::string, std::string>>& out) {
        JsonCursor c(text);
        if (!c.eat('{')) return false;
        for (;;) {
            if (c.eat('}')) return true;
            std::string k, v;
            if (!c.readString(k) || !c.eat(':') || !c.readString(v)) return false;
            out.emplace_back(std::move(k), std::move(v));
            c.eat(',');
        }
    }

    // Format 2: array [{"artist": "name", "val": ["city", ..., "Country"]}, ...].
    // The country is the LAST item of val; other keys are ignored.
    bool parseArtistValJson(const std::string& text, std::vector<std::pair<std::string, std::string>>& out) {
        JsonCursor c(text);
        if (!c.eat('[')) return false;
        for (;;) {
            if (c.eat(']')) return true;
            if (!c.eat('{')) return false;
            std::string artist, country;
            if (!c.eat('}')) {
                for (;;) {
                    std::string key;
                    if (!c.readString(key) || !c.eat(':')) return false;
                    if (key == "artist") {
                        if (!c.readString(artist)) return false;
                    } else if (key == "val") {
                        if (!c.eat('[')) return false;
                        if (!c.eat(']')) {
                            for (;;) {
                                std::string item;
                                if (!c.readString(item)) return false;
                                country = item;               // keep the last item
                                if (c.eat(',')) continue;
                                if (!c.eat(']')) return false;
                                break;
                            }
                        }
                    } else if (!c.skipValue()) {
                        return false;
                    }
                    if (c.eat(',')) continue;
                    if (!c.eat('}')) return false;
                    break;
                }
            }
            if (!artist.empty() && !country.empty()) out.emplace_back(std::move(artist), std::move(country));
            c.eat(',');
        }
    }

    struct ArtistTable {
        std::mutex m;
        std::shared_ptr<const ArtistMap> map;
        fs::file_time_type stamp{};
        bool haveStamp = false;
        std::chrono::steady_clock::time_point checked{};
        std::string path;   // configured location the table was loaded from ("" = default)
    };

    // Returns the current table (never null). Cheap: the file timestamp is checked at most
    // every 5 seconds and the table is only re-parsed when the timestamp changes, or at once
    // when the configured location changes. arrayFormat selects the file format:
    // false = flat {"artist": "country"}, true = [{"artist": ..., "val": [..., "country"]}].
    std::shared_ptr<const ArtistMap> loadArtistTable(ArtistTable& t, const std::string& custom,
        const char* defaultFile, bool arrayFormat) {
        using clock = std::chrono::steady_clock;
        std::lock_guard<std::mutex> lock(t.m);
        if (t.path != custom) {            // location changed in preferences: reload now
            t.path = custom;
            t.map.reset();
            t.haveStamp = false;
        }
        const auto now = clock::now();
        if (t.map && now - t.checked < std::chrono::seconds(5)) return t.map;
        t.checked = now;

        std::error_code ec;
        const fs::path p = custom.empty() ? componentDir() / defaultFile : fs::path(widenUtf8(custom));
        const auto stamp = fs::last_write_time(p, ec);
        if (ec) {
            // Missing file: no lookups.
            t.map = std::make_shared<const ArtistMap>();
            t.haveStamp = false;
            return t.map;
        }
        if (t.map && t.haveStamp && stamp == t.stamp) return t.map;

        t.stamp = stamp;
        t.haveStamp = true;
        std::ifstream f(p, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::vector<std::pair<std::string, std::string>> pairs;
        const bool ok = arrayFormat ? parseArtistValJson(text, pairs) : parseFlatJsonObject(text, pairs);
        if (!ok) {
            // Unreadable/invalid file: keep the previous table if there is one.
            if (!t.map) t.map = std::make_shared<const ArtistMap>();
            return t.map;
        }
        auto m = std::make_shared<ArtistMap>();
        m->reserve(pairs.size());
        for (auto& kv : pairs) {
            const std::string key = lookupKey(kv.first);
            if (key.empty()) continue;
            // Flat table: "US-NJ" -> "US". Array table: country names, which may contain '-'
            // ("Guinea-Bissau"), are used whole.
            const std::string country = arrayFormat ? kv.second : kv.second.substr(0, kv.second.find('-'));
            const std::string iso = normalize(country.c_str());
            if (iso.empty()) continue;
            if (arrayFormat) m->emplace(key, iso);   // first entry for an artist wins
            else (*m)[key] = iso;
        }
        t.map = m;
        return t.map;
    }

    // Table 1: artist_countries.json (flat). Table 2: worldMap.json (array, country = last item of val).
    std::shared_ptr<const ArtistMap> artistMap() {
        static ArtistTable t;
        return loadArtistTable(t, trim(g_cfgArtistFile.get()), "artist_countries.json", false);
    }

    std::shared_ptr<const ArtistMap> artistMap2() {
        static ArtistTable t;
        return loadArtistTable(t, trim(g_cfgArtistFile2.get()), "worldMap.json", true);
    }

    // Finds one artist name, also trying "The Cult" <-> "Cult, The".
    const std::string* findArtist(const ArtistMap& m, const std::string& name) {
        const std::string key = lookupKey(name);
        if (key.empty()) return nullptr;
        auto it = m.find(key);
        if (it != m.end()) return &it->second;

        std::string base;
        if (key.size() > 4 && key.compare(0, 4, "the ") == 0) base = key.substr(4);
        else if (key.size() > 5 && key.compare(key.size() - 5, 5, ", the") == 0) base = key.substr(0, key.size() - 5);
        if (base.empty()) return nullptr;
        for (const std::string& variant : { base + ", the", std::string("the ") + base, base }) {
            it = m.find(variant);
            if (it != m.end()) return &it->second;
        }
        return nullptr;
    }

    // Artist(s) from the playing stream's dynamic metadata (e.g. ICY StreamTitle), which is
    // not part of the static tags of the track. Captured via play callbacks so no playback
    // call is needed from whatever thread evaluates the field. Heap-allocated and released
    // on quit (it holds a service pointer).
    struct DynamicArtists {
        std::mutex m;
        metadb_handle_ptr track;
        std::vector<std::string> artists;
    };

    DynamicArtists& dynamicArtists() {
        static DynamicArtists* d = new DynamicArtists;
        return *d;
    }

    class dynamic_artist_callback : public play_callback_static {
    public:
        unsigned get_flags() override {
            return flag_on_playback_new_track | flag_on_playback_stop | flag_on_playback_dynamic_info_track;
        }
        void on_playback_new_track(metadb_handle_ptr p_track) override {
            DynamicArtists& d = dynamicArtists();
            std::lock_guard<std::mutex> lock(d.m);
            d.track = p_track;
            d.artists.clear();
        }
        void on_playback_stop(play_control::t_stop_reason) override {
            DynamicArtists& d = dynamicArtists();
            std::lock_guard<std::mutex> lock(d.m);
            d.track.release();
            d.artists.clear();
        }
        void on_playback_dynamic_info_track(const file_info& p_info) override {
            std::vector<std::string> artists;
            const t_size idx = p_info.meta_find("artist");
            if (idx != pfc_infinite) {
                const t_size count = p_info.meta_enum_value_count(idx);
                for (t_size k = 0; k < count; ++k) artists.emplace_back(p_info.meta_enum_value(idx, k));
            }
            DynamicArtists& d = dynamicArtists();
            std::lock_guard<std::mutex> lock(d.m);
            d.artists = std::move(artists);
        }
        void on_playback_starting(play_control::t_track_command, bool) override {}
        void on_playback_seek(double) override {}
        void on_playback_pause(bool) override {}
        void on_playback_edited(metadb_handle_ptr) override {}
        void on_playback_dynamic_info(const file_info&) override {}
        void on_playback_time(double) override {}
        void on_volume_change(float) override {}
    };
    play_callback_static_factory_t<dynamic_artist_callback> g_dynamic_artist_callback;

    // Looks up the artist(s) of a track: first the dynamic stream artist (only for the
    // playing track), then every "artist" value of its tags, then "album artist".
    std::vector<std::string> artistLookupCountries(metadb_handle* h) {
        std::vector<std::string> countries;
        const auto map = artistMap();
        const auto map2 = artistMap2();
        if ((!map || map->empty()) && (!map2 || map2->empty())) return countries;
        auto add = [&](const std::string& name) {
            // Table 1 first; table 2 only for artists missing from table 1.
            const std::string* iso = map ? findArtist(*map, name) : nullptr;
            if (!iso && map2) iso = findArtist(*map2, name);
            if (iso && std::find(countries.begin(), countries.end(), *iso) == countries.end())
                countries.push_back(*iso);
            };

        std::vector<std::string> dynamic;
        {
            DynamicArtists& d = dynamicArtists();
            std::lock_guard<std::mutex> lock(d.m);
            if (d.track.is_valid() && d.track.get_ptr() == h) dynamic = d.artists;
        }
        for (const auto& name : dynamic) add(name);
        if (!countries.empty()) return countries;

        metadb_info_container::ptr ref;
        if (!h->get_info_ref(ref) || ref.is_empty()) return countries;
        const file_info& info = ref->info();
        for (const char* field : { "artist", "album artist" }) {
            const t_size idx = info.meta_find(field);
            if (idx == pfc_infinite) continue;
            const t_size count = info.meta_enum_value_count(idx);
            for (t_size k = 0; k < count; ++k) add(std::string(info.meta_enum_value(idx, k)));
            if (!countries.empty()) break;
        }
        return countries;
    }

    // Compiled title formatting script. Recompiled only when the setting string changes.
    // The cache is heap-allocated and released explicitly on shutdown so no service
    // pointer is destroyed during static destruction after the core has gone.
    struct ScriptCache {
        std::mutex m;
        std::string raw;
        titleformat_object::ptr obj;
        bool init = false;
    };

    ScriptCache& sourceScriptCache(size_t index) {
        static ScriptCache* caches[5] = {
            new ScriptCache, new ScriptCache, new ScriptCache, new ScriptCache, new ScriptCache
        };
        return *caches[index];
    }

    cfg_string* sourceConfig(size_t index) {
        static cfg_string* configs[5] = {
            &g_cfgSource1, &g_cfgSource2, &g_cfgSource3, &g_cfgSource4, &g_cfgSource5
        };
        return configs[index];
    }

    titleformat_object::ptr compiledScript(size_t index) {
        const char* src = sourceConfig(index)->get();
        if (src == nullptr) src = "";

        ScriptCache& c = sourceScriptCache(index);
        std::lock_guard<std::mutex> lock(c.m);
        if (!c.init || c.raw != src) {
            titleformat_object::ptr obj;
            titleformat_compiler::get()->compile_safe(obj, src);
            c.obj = obj;
            c.raw = src;
            c.init = true;
        }
        return c.obj;
    }

    class quit_cleanup : public initquit {
    public:
        void on_quit() override {
            for (size_t i = 0; i < 5; ++i) {
                ScriptCache& c = sourceScriptCache(i);
                std::lock_guard<std::mutex> lock(c.m);
                c.obj.release();
                c.init = false;
            }
            DynamicArtists& d = dynamicArtists();
            std::lock_guard<std::mutex> lock(d.m);
            d.track.release();
            d.artists.clear();
        }
    };
    initquit_factory_t<quit_cleanup> g_quit_cleanup;

    // Resolve countries using only the selected source (artist-country or track-country).
    // A source may contain a list such as "Greece, Cyprus"; each recognised item is returned.
    std::vector<std::string> countriesOf(metadb_handle* h, size_t sourceIndex) {
        // The scripts must not use this component's own fields: that would recurse.
        thread_local bool inside = false;
        if (inside) return {};
        struct Guard {
            bool& f;
            explicit Guard(bool& x) : f(x) { f = true; }
            ~Guard() { f = false; }
        } guard(inside);

        std::vector<std::string> countries;
        titleformat_object::ptr script = compiledScript(sourceIndex);
        if (script.is_empty()) return countries;

        pfc::string8 result;
        h->format_title(nullptr, result, script, nullptr);
        const std::string whole = trim(result.c_str());
        if (whole.empty()) return countries;

        size_t pos = 0;
        while (pos <= whole.size()) {
            size_t end = whole.find_first_of(",;/", pos);
            if (end == std::string::npos) end = whole.size();
            const std::string piece = trim(whole.substr(pos, end - pos).c_str());
            const std::string iso = normalize(piece.c_str());
            if (!iso.empty() && std::find(countries.begin(), countries.end(), iso) == countries.end())
                countries.push_back(iso);
            if (end == whole.size()) break;
            pos = end + 1;
        }
        return countries;
    }

    // --- dedicated Preferences page -------------------------------------------

    const GUID guid_country_flags_preferences = {
        0x5e8c71a2, 0x9f24, 0x4a01, { 0x91, 0x62, 0x2b, 0x7e, 0x51, 0x0a, 0x44, 0x30 }
    };

    constexpr int ID_SOURCE1 = 1001;
    constexpr int ID_SOURCE2 = 1002;
    constexpr int ID_SOURCE3 = 1003;
    constexpr int ID_SOURCE4 = 1004;
    constexpr int ID_SOURCE5 = 1005;
    constexpr int ID_FLAG_DIR = 1006;
    constexpr int ID_PREFIX1 = 1007;
    constexpr int ID_PREFIX2 = 1008;
    constexpr int ID_PREFIX3 = 1009;
    constexpr int ID_PREFIX4 = 1010;
    constexpr int ID_PREFIX5 = 1011;
    constexpr int ID_BROWSE = 1012;
    constexpr int ID_ARTIST_FILE = 1013;
    constexpr int ID_ARTIST_BROWSE = 1014;
    constexpr int ID_ARTIST2_FILE = 1015;
    constexpr int ID_ARTIST2_BROWSE = 1016;
    constexpr const char* kFixedText = "(fixed - no suffix)";

    class country_flags_preferences_instance : public preferences_page_instance {
    public:
        country_flags_preferences_instance(HWND parent, preferences_page_callback::ptr callback)
            : m_callback(callback) {
            WNDCLASSW wc{};
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = core_api::get_my_instance();
            wc.lpszClassName = L"CountryFlagsPreferencesPage";
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = nullptr;
            RegisterClassW(&wc);

            m_wnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE,
                0, 0, 640, 580, parent, nullptr, wc.hInstance, this);
            if (m_wnd) {
                createControls();
                loadValues(false);
                // Use foobar2000's own theme state and update hooks, not the Windows
                // AppsUseLightTheme registry value (which can differ from foobar2000).
                m_darkMode.AddDialogWithControls(m_wnd);
            }
        }

        ~country_flags_preferences_instance() = default;

        t_uint32 get_state() override {
            t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
            if (m_wnd && hasChanges()) state |= preferences_state::changed;
            return state;
        }

        fb2k::hwnd_t get_wnd() override { return m_wnd; }

        void apply() override {
            if (!m_wnd) return;
            saveEdit(ID_SOURCE1, g_cfgSource1);
            saveEdit(ID_SOURCE2, g_cfgSource2);
            saveEdit(ID_SOURCE3, g_cfgSource3);
            saveEdit(ID_SOURCE4, g_cfgSource4);
            saveEdit(ID_SOURCE5, g_cfgSource5);
            saveEdit(ID_FLAG_DIR, g_cfgDir);
            saveEdit(ID_ARTIST_FILE, g_cfgArtistFile);
            saveEdit(ID_ARTIST2_FILE, g_cfgArtistFile2);
            // Source 1 has fixed legacy field names without a suffix. Sources 2-5
            // require a suffix; blank/invalid-only values fall back to defaults.
            const int prefixIDs[] = { ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5 };
            for (size_t i = 0; i < 4; ++i) {
                const std::string suffix = cleanSuffix(readEdit(prefixIDs[i]), i + 1);
                pfc::stringcvt::string_wide_from_utf8 wide(suffix.c_str());
                SetWindowTextW(GetDlgItem(m_wnd, prefixIDs[i]), wide.get_ptr());
            }
            saveEdit(ID_PREFIX2, g_cfgPrefix2);
            saveEdit(ID_PREFIX3, g_cfgPrefix3);
            saveEdit(ID_PREFIX4, g_cfgPrefix4);
            saveEdit(ID_PREFIX5, g_cfgPrefix5);
            captureOriginals();
            if (m_callback.is_valid()) m_callback->on_state_changed();
        }

        void reset() override {
            if (!m_wnd) return;
            setEdit(ID_SOURCE1, "%country%");
            setEdit(ID_SOURCE2, "$if2(%artistcountry%,%countryflag_lookup%)");
            setEdit(ID_SOURCE3, "");
            setEdit(ID_SOURCE4, "");
            setEdit(ID_SOURCE5, "");
            setEdit(ID_FLAG_DIR, "");
            setEdit(ID_ARTIST_FILE, "");
            setEdit(ID_ARTIST2_FILE, "");
            setEdit(ID_PREFIX1, kFixedText);
            setEdit(ID_PREFIX2, "artist");
            setEdit(ID_PREFIX3, "3");
            setEdit(ID_PREFIX4, "4");
            setEdit(ID_PREFIX5, "5");
            if (m_callback.is_valid()) m_callback->on_state_changed();
        }

    private:
        static LRESULT CALLBACK WindowProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
            country_flags_preferences_instance* self =
                reinterpret_cast<country_flags_preferences_instance*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                self = static_cast<country_flags_preferences_instance*>(cs->lpCreateParams);
                SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            if (self) return self->handleMessage(wnd, msg, wp, lp);
            return DefWindowProcW(wnd, msg, wp, lp);
        }

        LRESULT handleMessage(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
            // STATIC label controls otherwise paint an opaque light background on
            // some Windows/foobar2000 theme combinations. Make their background
            // transparent so the core dark-mode dialog background shows through.
            if (msg == WM_CTLCOLORSTATIC) {
                HDC hdc = reinterpret_cast<HDC>(wp);
                SetBkMode(hdc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
            }
            if (msg == WM_COMMAND && LOWORD(wp) == ID_BROWSE && HIWORD(wp) == BN_CLICKED) {
                browseForFlagFolder();
                return 0;
            }
            if (msg == WM_COMMAND && LOWORD(wp) == ID_ARTIST_BROWSE && HIWORD(wp) == BN_CLICKED) {
                browseForArtistFile(ID_ARTIST_FILE);
                return 0;
            }
            if (msg == WM_COMMAND && LOWORD(wp) == ID_ARTIST2_BROWSE && HIWORD(wp) == BN_CLICKED) {
                browseForArtistFile(ID_ARTIST2_FILE);
                return 0;
            }
            if (msg == WM_COMMAND && HIWORD(wp) == EN_CHANGE) {
                if (m_callback.is_valid()) m_callback->on_state_changed();
            }
            if (msg == WM_SIZE) layoutControls(LOWORD(lp), HIWORD(lp));
            if (msg == WM_THEMECHANGED || msg == WM_SYSCOLORCHANGE || msg == WM_SETTINGCHANGE) {
                InvalidateRect(wnd, nullptr, TRUE);
                EnumChildWindows(wnd, [](HWND child, LPARAM) -> BOOL { InvalidateRect(child, nullptr, TRUE); return TRUE; }, 0);
            }
            if (msg == WM_DESTROY) m_wnd = nullptr;
            return DefWindowProcW(wnd, msg, wp, lp);
        }

        void createControls() {
            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const HINSTANCE inst = core_api::get_my_instance();
            auto addLabel = [&](const wchar_t* text) {
                // SS_NOPREFIX: never treat '&' in label text as an accelerator marker.
                HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                    0, 0, 10, 10, m_wnd, nullptr, inst, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return c;
                };
            auto addEdit = [&](int id) {
                HWND c = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    0, 0, 10, 10, m_wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return c;
                };

            // Two columns per source: title-formatting script | field suffix.
            m_hdrScript = addLabel(L"Title-formatting script");
            m_hdrSuffix = addLabel(L"Field suffix");

            const wchar_t* sourceTitles[] = { L"Source 1", L"Source 2", L"Source 3", L"Source 4", L"Source 5" };
            const int scriptIDs[] = { ID_SOURCE1, ID_SOURCE2, ID_SOURCE3, ID_SOURCE4, ID_SOURCE5 };
            const int prefixIDs[] = { ID_PREFIX1, ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5 };
            for (int i = 0; i < 5; ++i) {
                m_rowLabel[i] = addLabel(sourceTitles[i]);
                addEdit(scriptIDs[i]);
                addEdit(prefixIDs[i]);
            }
            // Source 1 keeps the original unsuffixed field names.
            setEdit(ID_PREFIX1, kFixedText);
            EnableWindow(GetDlgItem(m_wnd, ID_PREFIX1), FALSE);

            m_note = addLabel(L"Source 1 uses the fixed fields %countryflag_emoji%, %countryflag_path%, ... "
                L"Sources 2-5 use %countryflag_<suffix>_emoji% etc.: enter only the suffix, not \"countryflag_\". "
                L"Restart foobar2000 after changing a suffix.");
            m_flagLabel = addLabel(L"Flag image folder (empty = component's flags folder)");
            addEdit(ID_FLAG_DIR);
            HWND browse = CreateWindowExW(0, L"BUTTON", L"Browse...",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, m_wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BROWSE)), inst, nullptr);
            SendMessageW(browse, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            m_artistLabel = addLabel(L"Artist table 1, artist_countries.json: {\"artist\": \"CC\"} (empty = next to the component)");
            addEdit(ID_ARTIST_FILE);
            HWND browseArtist = CreateWindowExW(0, L"BUTTON", L"Browse...",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, m_wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ARTIST_BROWSE)), inst, nullptr);
            SendMessageW(browseArtist, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            m_artist2Label = addLabel(L"Artist table 2, worldMap.json: last item of val = country (empty = next to the component)");
            addEdit(ID_ARTIST2_FILE);
            HWND browseArtist2 = CreateWindowExW(0, L"BUTTON", L"Browse...",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, m_wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ARTIST2_BROWSE)), inst, nullptr);
            SendMessageW(browseArtist2, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            layoutControls(640, 300);
        }

        void layoutControls(int width, int) {
            if (!m_wnd) return;
            const int margin = 12, gap = 8, labelW = 56, rowH = 28, rowStep = 34, browseW = 90;
            const int fullW = (std::max)(100, width - 2 * margin);
            const int suffixW = (std::min)(180, (std::max)(90, width / 4));
            const int xLabel = margin;
            const int xScript = xLabel + labelW + gap;
            const int scriptW = (std::max)(80, width - margin - suffixW - gap - xScript);
            const int xSuffix = xScript + scriptW + gap;

            auto place = [&](HWND h, int x, int y, int w, int hgt) {
                if (h) SetWindowPos(h, nullptr, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
                };
            const int scriptIDs[] = { ID_SOURCE1, ID_SOURCE2, ID_SOURCE3, ID_SOURCE4, ID_SOURCE5 };
            const int prefixIDs[] = { ID_PREFIX1, ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5 };

            int y = 10;
            place(m_hdrScript, xScript, y, scriptW, 16);
            place(m_hdrSuffix, xSuffix, y, suffixW, 16);
            y += 19;
            for (int i = 0; i < 5; ++i) {
                place(m_rowLabel[i], xLabel, y + 6, labelW, 16);
                place(GetDlgItem(m_wnd, scriptIDs[i]), xScript, y, scriptW, rowH);
                place(GetDlgItem(m_wnd, prefixIDs[i]), xSuffix, y, suffixW, rowH);
                y += rowStep;
            }
            y += 6;
            place(m_note, margin, y, fullW, 48);
            y += 54;
            place(m_flagLabel, margin, y, fullW, 16);
            y += 19;
            place(GetDlgItem(m_wnd, ID_FLAG_DIR), margin, y, (std::max)(60, fullW - browseW - gap), rowH);
            place(GetDlgItem(m_wnd, ID_BROWSE), margin + (std::max)(60, fullW - browseW - gap) + gap, y, browseW, rowH);
            y += rowH + 12;
            place(m_artistLabel, margin, y, fullW, 16);
            y += 19;
            place(GetDlgItem(m_wnd, ID_ARTIST_FILE), margin, y, (std::max)(60, fullW - browseW - gap), rowH);
            place(GetDlgItem(m_wnd, ID_ARTIST_BROWSE), margin + (std::max)(60, fullW - browseW - gap) + gap, y, browseW, rowH);
            y += rowH + 12;
            place(m_artist2Label, margin, y, fullW, 16);
            y += 19;
            place(GetDlgItem(m_wnd, ID_ARTIST2_FILE), margin, y, (std::max)(60, fullW - browseW - gap), rowH);
            place(GetDlgItem(m_wnd, ID_ARTIST2_BROWSE), margin + (std::max)(60, fullW - browseW - gap) + gap, y, browseW, rowH);

            // Labels are drawn with a transparent background; repaint parent and children
            // together so moved/resized labels never overpaint their old text.
            RedrawWindow(m_wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        }

        void browseForFlagFolder() {
            IFileOpenDialog* dlg = nullptr;
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg)
                return;
            DWORD opts = 0;
            dlg->GetOptions(&opts);
            dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            dlg->SetTitle(L"Select flag image folder");

            // Start in the currently entered folder, if it exists.
            const std::string current = trim(readEdit(ID_FLAG_DIR).c_str());
            if (!current.empty()) {
                pfc::stringcvt::string_wide_from_utf8 wcur(current.c_str());
                IShellItem* folder = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(wcur.get_ptr(), nullptr, IID_PPV_ARGS(&folder))) && folder) {
                    dlg->SetFolder(folder);
                    folder->Release();
                }
            }

            if (SUCCEEDED(dlg->Show(m_wnd))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                        SetWindowTextW(GetDlgItem(m_wnd, ID_FLAG_DIR), path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dlg->Release();
        }

        void browseForArtistFile(int editId) {
            IFileOpenDialog* dlg = nullptr;
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg)
                return;
            DWORD opts = 0;
            dlg->GetOptions(&opts);
            dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
            dlg->SetTitle(L"Select artist table (JSON)");
            const COMDLG_FILTERSPEC filters[] = { { L"JSON files (*.json)", L"*.json" }, { L"All files (*.*)", L"*.*" } };
            dlg->SetFileTypes(2, filters);
            dlg->SetDefaultExtension(L"json");

            // Start in the folder of the currently entered file, if any.
            const std::string current = trim(readEdit(editId).c_str());
            if (!current.empty()) {
                pfc::stringcvt::string_wide_from_utf8 wcur(current.c_str());
                const fs::path cur(wcur.get_ptr());
                const std::wstring parent = cur.parent_path().wstring();
                IShellItem* folder = nullptr;
                if (!parent.empty() && SUCCEEDED(SHCreateItemFromParsingName(parent.c_str(), nullptr, IID_PPV_ARGS(&folder))) && folder) {
                    dlg->SetFolder(folder);
                    folder->Release();
                }
                const std::wstring name = cur.filename().wstring();
                if (!name.empty()) dlg->SetFileName(name.c_str());
            }

            if (SUCCEEDED(dlg->Show(m_wnd))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                        SetWindowTextW(GetDlgItem(m_wnd, editId), path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dlg->Release();
        }

        std::string readEdit(int id) const {
            HWND edit = GetDlgItem(m_wnd, id);
            const int len = GetWindowTextLengthW(edit);
            std::wstring wide(static_cast<size_t>(len) + 1, L'\0');
            GetWindowTextW(edit, wide.data(), len + 1);
            wide.resize(len);
            pfc::stringcvt::string_utf8_from_wide utf8(wide.c_str());
            return std::string(utf8.get_ptr());
        }

        void setEdit(int id, const char* value) {
            pfc::stringcvt::string_wide_from_utf8 wide(value);
            SetWindowTextW(GetDlgItem(m_wnd, id), wide.get_ptr());
        }

        void saveEdit(int id, cfg_string& cfg) {
            const std::string value = readEdit(id);
            cfg = value.c_str();
        }

        void loadValues(bool defaults) {
            if (defaults) { reset(); return; }
            setEdit(ID_SOURCE1, g_cfgSource1.get());
            setEdit(ID_SOURCE2, g_cfgSource2.get());
            setEdit(ID_SOURCE3, g_cfgSource3.get());
            setEdit(ID_SOURCE4, g_cfgSource4.get());
            setEdit(ID_SOURCE5, g_cfgSource5.get());
            setEdit(ID_FLAG_DIR, g_cfgDir.get());
            setEdit(ID_ARTIST_FILE, g_cfgArtistFile.get());
            setEdit(ID_ARTIST2_FILE, g_cfgArtistFile2.get());
            setEdit(ID_PREFIX1, kFixedText);
            setEdit(ID_PREFIX2, cleanSuffix(trim(g_cfgPrefix2.get()), 1).c_str());
            setEdit(ID_PREFIX3, cleanSuffix(trim(g_cfgPrefix3.get()), 2).c_str());
            setEdit(ID_PREFIX4, cleanSuffix(trim(g_cfgPrefix4.get()), 3).c_str());
            setEdit(ID_PREFIX5, cleanSuffix(trim(g_cfgPrefix5.get()), 4).c_str());
            captureOriginals();
        }

        void captureOriginals() {
            for (int id : { ID_SOURCE1, ID_SOURCE2, ID_SOURCE3, ID_SOURCE4, ID_SOURCE5, ID_FLAG_DIR, ID_ARTIST_FILE, ID_ARTIST2_FILE, ID_PREFIX1, ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5 })
                m_original[id] = readEdit(id);
        }

        bool hasChanges() const {
            for (int id : { ID_SOURCE1, ID_SOURCE2, ID_SOURCE3, ID_SOURCE4, ID_SOURCE5, ID_FLAG_DIR, ID_ARTIST_FILE, ID_ARTIST2_FILE, ID_PREFIX1, ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5 }) {
                const auto it = m_original.find(id);
                if (it == m_original.end() || it->second != readEdit(id)) return true;
            }
            return false;
        }

        HWND m_wnd = nullptr;
        HWND m_hdrScript = nullptr, m_hdrSuffix = nullptr, m_note = nullptr, m_flagLabel = nullptr, m_artistLabel = nullptr, m_artist2Label = nullptr;
        HWND m_rowLabel[5] = {};
        fb2k::CCoreDarkModeHooks m_darkMode;
        preferences_page_callback::ptr m_callback;
        std::unordered_map<int, std::string> m_original;
    };

    class country_flags_preferences_page : public preferences_page_v3 {
    public:
        const char* get_name() override { return "Country Flags"; }
        GUID get_guid() override { return guid_country_flags_preferences; }
        GUID get_parent_guid() override { return preferences_page::guid_tools; }
        preferences_page_instance::ptr instantiate(fb2k::hwnd_t parent, preferences_page_callback::ptr callback) override {
            // Wrap the implementation in service_impl_t so service_base's
            // reference-counting and service-query methods are supplied by the SDK.
            return new service_impl_t<country_flags_preferences_instance>(reinterpret_cast<HWND>(parent), callback);
        }
    };
    preferences_page_factory_t<country_flags_preferences_page> g_country_flags_preferences_page;

    // --- field provider -------------------------------------------------------

    class flag_fields : public metadb_display_field_provider {
    public:
        // 25 source fields (5 sources x 5 kinds) + %countryflag_lookup%.
        t_uint32 get_field_count() override { return 26; }

        void get_field_name(t_uint32 index, pfc::string_base& out) override {
            if (index == 25) { out.set_string("countryflag_lookup"); return; }
            if (index > 25) { out.reset(); return; }
            static const char* suffixes[] = { "emoji", "path", "name", "iso2", "iso3" };
            const std::string prefix = fieldPrefix(index / 5);
            if (prefix.empty()) { out.set_string(""); return; }
            const std::string name = prefix + suffixes[index % 5];
            out.set_string(name.c_str());
        }

        bool process_field(t_uint32 index, metadb_handle* handle, titleformat_text_out* out) override {
            if (index == 25) {
                // ISO2 code(s) of the artist(s) found in artist_countries.json, for use in source scripts.
                std::string value;
                for (const auto& iso : artistLookupCountries(handle)) {
                    if (!value.empty()) value += "; ";
                    value += iso;
                }
                if (value.empty()) return false;
                out->write(titleformat_inputtypes::meta, value.c_str(), value.length());
                return true;
            }
            const size_t sourceIndex = index / 5;
            const t_uint32 kind = index % 5;
            const std::vector<std::string> countries = countriesOf(handle, sourceIndex);
            if (countries.empty()) return false;

            std::string value;
            const char* separator = kind == 0 ? " " : "; ";
            for (const auto& iso : countries) {
                std::string part;
                switch (kind) {
                case 0: part = emojiFlag(iso); break;
                case 1: part = flagPath(iso); break;
                case 2: { auto it = db().a2name.find(iso); if (it != db().a2name.end()) part = it->second; break; }
                case 3: part = iso; break;
                case 4: { auto it = db().a2to3.find(iso); if (it != db().a2to3.end()) part = it->second; break; }
                }
                if (part.empty()) continue;
                if (!value.empty()) value += separator;
                value += part;
            }
            if (value.empty()) return false;
            out->write(titleformat_inputtypes::meta, value.c_str(), value.length());
            return true;
        }
    };
    service_factory_single_t<flag_fields> g_flag_fields;

} // namespace
