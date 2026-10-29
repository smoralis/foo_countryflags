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
// Defaults: source 1 = %country%, source 2 = %artistcountry% (fallback: artist name); sources 3-5 empty.
// Each source has an optional FALLBACK script: when the main script returns no recognised
// country, the fallback is evaluated and its result (an artist name) is looked up in the
// artist tables (worldMap.json first, then country_codes.json).
// Paths in preferences may use %fb2k_profile_path%, %fb2k_component_path%, %fb2k_path% and
// Windows %VARIABLES%; relative paths are tried against the foobar2000 folder, then the profile.
// Fields per source: emoji, path, name, iso2, iso3, region, subregion (cultural regions, e.g.
// SLE -> Africa / West Africa; built-in table, extendable with regions.txt).
// Extra field: %countryflag_lookup% = ISO2 code(s) of the track's artist(s) found in the artist tables
//   (next to the DLL; flat JSON {"artist name": "CC"}, case-insensitive, reloaded when changed).
//   For the playing stream, the dynamic (ICY) artist is searched first.
//   The file location can be set in preferences (empty = next to the DLL).
//   worldMap.json ([{"artist": "...", "val": [..., "Country"]}]; the last item of val is the
//   country) and country_codes.json are both searched; which one takes precedence when an
//   artist is in both is a setting (default: worldMap.json first).
// Preferences > Tools > Country Flags.
// The script must not use the per-source %countryflag_*% fields (re-entrant calls are
// ignored to prevent infinite recursion). %countryflag_lookup% is safe to use in scripts.
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
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iterator>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <shobjidl.h>
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Comctl32.lib")

DECLARE_COMPONENT_VERSION("Country Flags", "0.1.4",
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
    const GUID guid_fallback1 = { 0x7f6a1041, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x21 } };
    const GUID guid_fallback2 = { 0x7f6a1042, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x22 } };
    const GUID guid_fallback3 = { 0x7f6a1043, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x23 } };
    const GUID guid_fallback4 = { 0x7f6a1044, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x24 } };
    const GUID guid_fallback5 = { 0x7f6a1045, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x25 } };
    const GUID guid_table_order = { 0x5b2e9a61, 0x0c73, 0x4e18, { 0x8d, 0x4a, 0x92, 0x1e, 0x6f, 0x30, 0xb7, 0xd5 } };
    const GUID guid_dir = { 0xb46e52be, 0x3476, 0x45b5, { 0x98, 0x4f, 0xff, 0x11, 0xbd, 0x1f, 0x98, 0x8b } };
    const GUID guid_prefix1 = { 0x7f6a1031, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x11 } };
    const GUID guid_prefix2 = { 0x7f6a1032, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x12 } };
    const GUID guid_prefix3 = { 0x7f6a1033, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x13 } };
    const GUID guid_prefix4 = { 0x7f6a1034, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x14 } };
    const GUID guid_prefix5 = { 0x7f6a1035, 0x2b44, 0x4c9d, { 0x91, 0x31, 0x3e, 0x67, 0x18, 0x22, 0x4a, 0x15 } };


    cfg_string g_cfgSource1(guid_source1_script, "%country%");
    constexpr const char* kDefaultSource2 = "%artistcountry%";
    constexpr const char* kDefaultFallback2 = "$if3(%artist%,%album artist%)";

    cfg_string g_cfgSource2(guid_source2_script, kDefaultSource2);
    cfg_string g_cfgSource3(guid_source3_script, "");
    cfg_string g_cfgSource4(guid_source4_script, "");
    cfg_string g_cfgSource5(guid_source5_script, "");
    cfg_string g_cfgFallback1(guid_fallback1, "");
    cfg_string g_cfgFallback2(guid_fallback2, kDefaultFallback2);
    cfg_string g_cfgFallback3(guid_fallback3, "");
    cfg_string g_cfgFallback4(guid_fallback4, "");
    cfg_string g_cfgFallback5(guid_fallback5, "");
    cfg_string g_cfgDir(guid_dir, "");
    cfg_int g_cfgTableOrder(guid_table_order, 0);
    cfg_string g_cfgArtistFile(guid_artist_file, "");
    cfg_string g_cfgArtistFile2(guid_artist_file2, "");
    cfg_string g_cfgPrefix1(guid_prefix1, "main");
    cfg_string g_cfgPrefix2(guid_prefix2, "artist");
    cfg_string g_cfgPrefix3(guid_prefix3, "3");
    cfg_string g_cfgPrefix4(guid_prefix4, "4");
    cfg_string g_cfgPrefix5(guid_prefix5, "5");

    std::atomic<unsigned> g_generation{ 0 };

    std::string trim(const char* s);
    fs::path expandPath(const std::string& utf8);

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
        if (index == 0) return "countryflag_";
        const std::string suffix = cleanSuffix(trim(prefixConfig(index)->get()), index);
        return std::string("countryflag_") + suffix + "_";
    }

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

    struct RegionRow { const char* region; const char* subregion; const char* iso3; };

    const RegionRow kRegionRows[] = {
        { "Antarctica", "Antarctica", "ATA" },
        { "Africa", "West Africa", "SLE NGA LBR GMB GHA GNB GIN SEN CIV BEN TGO CPV MLI MRT BFA NER" },
        { "Africa", "Maghreb", "DZA TUN ESH MAR LBY EGY" },
        { "Africa", "Central Africa", "COD COG GAB GNQ CMR RWA CAF TCD BDI" },
        { "Africa", "East Africa", "ETH KEN SDN SOM DJI ERI UGA SSD" },
        { "Africa", "South Africa", "ZAF TZA MOZ NAM ZMB ZWE COM SWZ SYC STP SHN REU AGO MDG BWA ATF LSO MWI MUS MYT BVT" },
        { "Asia", "Central Asia", "NPL BTN UZB KGZ TKM TJK" },
        { "Asia", "East Asia", "TWN CHN JPN PRK MAC HKG KOR" },
        { "Asia", "West Asia", "IRN PAK AFG" },
        { "Asia", "South Asia", "BGD THA IND MMR VNM LKA SGP TLS PHL MYS LAO KHM MDV BRN IOT IDN" },
        { "Asia", "North Asia", "MNG RUS KAZ" },
        { "Europe", "Eastern Europe", "ALB UKR SRB MKD HRV MDA MNE ROU BIH BGR XKX BLR RUS" },
        { "Europe", "Southern Europe", "AND ESP ITA GIB GRC PRT MLT SMR VAT" },
        { "Europe", "Central Europe", "AUT CHE SVN SVK DEU CZE LIE POL HUN" },
        { "Europe", "Western Europe", "BEL GBR MCO IRL FRA LUX NLD GGY IMN JEY" },
        { "Europe", "Northern Europe", "SWE NOR LTU EST DNK FIN SJM LVA GRL FRO ISL ALA" },
        { "Mashriq", "Arabian Peninsula", "BHR SAU OMN QAT YEM ARE" },
        { "Mashriq", "Anatolia", "AZE ARM TUR GEO" },
        { "Mashriq", "Levant", "SYR ISR PSE LBN JOR CYP" },
        { "Mashriq", "Mesopotamia", "IRQ KWT" },
        { "America", "Caribbean", "CUB DMA DOM JAM PRI MSR VIR VGB TCA TTO VCT LCA KNA HTI CYM MTQ ANT GLP GRD AIA SXM MAF BLM CUW BES" },
        { "America", "North America", "ATG ABW BHS BRB USA CAN SPM BMU" },
        { "America", "Central America", "CRI SLV GTM HND MEX NIC PAN BLZ" },
        { "America", "South America", "ARG CHL COL ECU URY PRY PER VEN SUR SGS BOL GUF BRA FLK GUY" },
        { "Oceania", "Australasia", "AUS CCK CXR NFK HMD" },
        { "Oceania", "Melanesia", "VUT NCL SLB PNG FJI" },
        { "Oceania", "Micronesia", "PLW GUM FSM NRU MHL MNP" },
        { "Oceania", "Polynesia", "TUV WLF UMI TKL TON WSM PCN PYF COK NZL ASM NIU KIR" },
    };

    const RegionRow kRegionExtras[] = {
        { "Europe", "", "EUR" },
    };

    struct Db {
        std::unordered_map<std::string, std::string> a2name;
        std::unordered_map<std::string, std::string> a3to2;
        std::unordered_map<std::string, std::string> a2to3;
        std::unordered_map<std::string, std::string> name2to2;
        std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> a2region;
    };

    const Db& db() {
        static Db d;
        static std::once_flag once;
        std::call_once(once, [] {
            std::ifstream f(componentDir() / "countries.txt");
            std::string line;
            while (std::getline(f, line)) {
                if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line.erase(0, 3);
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
            auto addBuiltin = [&](const char* a2, const char* a3, const char* nm) {
                if (d.a2name.count(a2)) return;
                d.a2name[a2] = nm;
                d.a2to3[a2] = a3;
                d.a3to2.emplace(a3, a2);
                d.name2to2.emplace(lower(nm), a2);
                };
            addBuiltin("EU", "EUR", "Europe");
            addBuiltin("XW", "XWW", "Worldwide");

            const char* aliases[][2] = {
                {"united states of america", "US"}, {"czech republic", "CZ"}, {"turkiye", "TR"},
                {"t\xC3\xBC" "rkiye", "TR"}, {"burma", "MM"}, {"ivory coast", "CI"}, {"cape verde", "CV"},
                {"vatican", "VA"}, {"macedonia", "MK"}, {"swaziland", "SZ"}, {"east timor", "TL"},
                {"russian federation", "RU"}, {"korea", "KR"}, {"great britain", "GB"},
                {"england", "GB"}, {"scotland", "GB"}, {"wales", "GB"}, {"uk", "GB"},
                {"holland", "NL"}, {"hellas", "GR"},
                {"european union", "EU"}, {"world", "XW"},
                {"aland islands", "AX"}, {"\xC3\xA5land islands", "AX"},
                {"st. barts", "BL"}, {"st. barths", "BL"}, {"st. barth", "BL"},
                {"saint barthelemy", "BL"}, {"saint barth\xC3\xA9lemy", "BL"},
                {"bonaire", "BQ"},
                {"curacao", "CW"}, {"cura\xC3\xA7" "ao", "CW"}
            };
            for (auto& a : aliases) d.name2to2[a[0]] = a[1];

            auto addRegions = [&](const RegionRow* rows, size_t count) {
                for (size_t i = 0; i < count; ++i) {
                    std::istringstream codes(rows[i].iso3);
                    std::string code;
                    while (codes >> code) {
                        const auto it = d.a3to2.find(code);
                        if (it == d.a3to2.end()) continue;
                        auto& list = d.a2region[it->second];
                        const std::pair<std::string, std::string> entry{ rows[i].region, rows[i].subregion };
                        if (std::find(list.begin(), list.end(), entry) == list.end()) list.push_back(entry);
                    }
                }
                };
            addRegions(kRegionRows, sizeof(kRegionRows) / sizeof(kRegionRows[0]));
            addRegions(kRegionExtras, sizeof(kRegionExtras) / sizeof(kRegionExtras[0]));

            std::unordered_set<std::string> replaced;
            std::ifstream rf(componentDir() / "regions.txt");
            std::string rl;
            while (std::getline(rf, rl)) {
                if (rl.size() >= 3 && (unsigned char)rl[0] == 0xEF) rl.erase(0, 3);
                if (!rl.empty() && rl.back() == '\r') rl.pop_back();
                if (rl.empty() || rl[0] == '#') continue;
                const auto q1 = rl.find('|');
                if (q1 == std::string::npos) continue;
                const auto q2 = rl.find('|', q1 + 1);
                const std::string code = upper(trim(rl.substr(0, q1).c_str()));
                const std::string region = trim(rl.substr(q1 + 1, q2 == std::string::npos ? std::string::npos : q2 - q1 - 1).c_str());
                const std::string subregion = q2 == std::string::npos ? std::string() : trim(rl.substr(q2 + 1).c_str());
                std::string a2 = code;
                if (code.size() == 3) {
                    const auto it = d.a3to2.find(code);
                    a2 = it == d.a3to2.end() ? std::string() : it->second;
                }
                else if (code == "XE") {
                    a2 = "EU";
                }
                if (a2.empty() || !d.a2name.count(a2)) continue;
                if (replaced.insert(a2).second) d.a2region.erase(a2);
                if (region.empty() && subregion.empty()) continue;
                auto& list = d.a2region[a2];
                const std::pair<std::string, std::string> entry{ region, subregion };
                if (std::find(list.begin(), list.end(), entry) == list.end()) list.push_back(entry);
            }
            });
        return d;
    }

    std::string normalize(const char* raw) {
        std::string s = trim(raw);
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
        if (iso2 == "XW") return "\xF0\x9F\x8C\x90";
        std::string r;
        for (char c : iso2) {
            if (c < 'A' || c > 'Z') return {};
            r += "\xF0\x9F\x87";
            r += (char)(0xA6 + (c - 'A'));
        }
        return r;
    }

    std::string flagPath(const std::string& iso2) {
        using clock = std::chrono::steady_clock;
        struct Entry { std::string path; clock::time_point checked; };

        static std::mutex m;
        static bool init = false;
        static std::string lastDir;
        static std::unordered_map<std::string, Entry> cache;
        constexpr auto ttl = std::chrono::seconds(30);

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

        fs::path dir = customDir.empty() ? componentDir() / "flags" : expandPath(customDir);

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

    const std::wstring& profileDirWide() {
        static const std::wstring dir = [] {
            std::wstring result;
            const char* url = core_api::get_profile_path();
            if (!url) return result;
            std::string n = url;
            if (n.rfind("file://", 0) == 0) n.erase(0, 7);
            for (auto& c : n) if (c == '/') c = '\\';
            while (n.size() > 3 && n.back() == '\\') n.pop_back();
            return widenUtf8(n);
            }();
        return dir;
    }

    std::wstring exeDirWide() {
        wchar_t buf[MAX_PATH * 2] = {};
        const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
        if (n == 0) return {};
        return fs::path(std::wstring(buf, n)).parent_path().wstring();
    }

    void replaceTokenCI(std::wstring& s, const std::wstring& token, const std::wstring& value) {
        if (token.empty()) return;
        size_t pos = 0;
        for (;;) {
            size_t i = pos;
            bool found = false;
            for (; i + token.size() <= s.size(); ++i) {
                if (_wcsnicmp(s.c_str() + i, token.c_str(), token.size()) == 0) { found = true; break; }
            }
            if (!found) return;
            s.replace(i, token.size(), value);
            pos = i + value.size();
        }
    }

    fs::path expandPath(const std::string& utf8) {
        std::wstring w = widenUtf8(trim(utf8.c_str()));
        if (w.empty()) return {};
        replaceTokenCI(w, L"%fb2k_profile_path%", profileDirWide());
        replaceTokenCI(w, L"%fb2k_component_path%", componentDir().wstring());
        replaceTokenCI(w, L"%fb2k_path%", exeDirWide());
        if (w.find(L'%') != std::wstring::npos) {
            const DWORD need = ExpandEnvironmentStringsW(w.c_str(), nullptr, 0);
            if (need > 1) {
                std::wstring e((size_t)need, L'\0');
                const DWORD got = ExpandEnvironmentStringsW(w.c_str(), &e[0], need);
                if (got > 0 && got <= need) { e.resize((size_t)got - 1); w = e; }
            }
        }
        fs::path p(w);
        if (!p.has_root_name() && !p.has_root_directory()) {
            const fs::path inExe = fs::path(exeDirWide()) / p;
            std::error_code ec;
            if (fs::exists(inExe, ec)) return inExe;
            const std::wstring& prof = profileDirWide();
            if (!prof.empty()) {
                const fs::path inProfile = fs::path(prof) / p;
                if (fs::exists(inProfile, ec)) return inProfile;
            }
            return inExe;
        }
        return p;
    }

    fs::path defaultTablePath(const char* fileName, bool profileJsData) {
        const fs::path inComponent = componentDir() / fileName;
        if (!profileJsData) return inComponent;
        const std::wstring& prof = profileDirWide();
        if (prof.empty()) return inComponent;
        const fs::path inProfile = fs::path(prof) / L"js_data" / fileName;
        std::error_code ec;
        if (fs::exists(inProfile, ec)) return inProfile;
        if (fs::exists(inComponent, ec)) return inComponent;
        return inProfile;
    }

    std::wstring portablePath(const std::wstring& path) {
        const std::wstring& prof = profileDirWide();
        if (!prof.empty() && path.size() > prof.size() &&
            _wcsnicmp(path.c_str(), prof.c_str(), prof.size()) == 0 &&
            (path[prof.size()] == L'\\' || path[prof.size()] == L'/'))
            return L"%fb2k_profile_path%" + path.substr(prof.size());
        return path;
    }

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
            const size_t start = i;
            while (i < t.size() && t[i] != ',' && t[i] != ']' && t[i] != '}' && t[i] != ' ' && t[i] != '\t' && t[i] != '\r' && t[i] != '\n') ++i;
            return i > start;
        }
    };

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
                    }
                    else if (key == "val") {
                        if (!c.eat('[')) return false;
                        if (!c.eat(']')) {
                            for (;;) {
                                std::string item;
                                if (!c.readString(item)) return false;
                                country = item;
                                if (c.eat(',')) continue;
                                if (!c.eat(']')) return false;
                                break;
                            }
                        }
                    }
                    else if (!c.skipValue()) {
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
        std::string path;
    };

    std::shared_ptr<const ArtistMap> loadArtistTable(ArtistTable& t, const std::string& custom,
        const char* defaultFile, bool profileJsData, bool arrayFormat) {
        using clock = std::chrono::steady_clock;
        std::lock_guard<std::mutex> lock(t.m);
        if (t.path != custom) {
            t.path = custom;
            t.map.reset();
            t.haveStamp = false;
            ++g_generation;
        }
        const auto now = clock::now();
        if (t.map && now - t.checked < std::chrono::seconds(5)) return t.map;
        t.checked = now;

        std::error_code ec;
        const fs::path p = custom.empty() ? defaultTablePath(defaultFile, profileJsData) : expandPath(custom);
        const auto stamp = fs::last_write_time(p, ec);
        if (ec) {
            if (!t.map || !t.map->empty()) ++g_generation;
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
            if (!t.map) t.map = std::make_shared<const ArtistMap>();
            return t.map;
        }
        auto m = std::make_shared<ArtistMap>();
        m->reserve(pairs.size());
        for (auto& kv : pairs) {
            const std::string key = lookupKey(kv.first);
            if (key.empty()) continue;
            const std::string country = arrayFormat ? kv.second : kv.second.substr(0, kv.second.find('-'));
            const std::string iso = normalize(country.c_str());
            if (iso.empty()) continue;
            (*m)[key] = iso;
        }
        t.map = m;
        ++g_generation;
        return t.map;
    }

    std::shared_ptr<const ArtistMap> artistMap() {
        static ArtistTable t;
        return loadArtistTable(t, trim(g_cfgArtistFile.get()), "country_codes.json", false, false);
    }

    std::shared_ptr<const ArtistMap> artistMap2() {
        static ArtistTable t;
        return loadArtistTable(t, trim(g_cfgArtistFile2.get()), "worldMap.json", true, true);
    }

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

    const std::string* findPreferred(const std::shared_ptr<const ArtistMap>& flatMap,
        const std::shared_ptr<const ArtistMap>& worldMap, const std::string& name) {
        const bool flatFirst = g_cfgTableOrder.get_value() == 1;
        const std::shared_ptr<const ArtistMap>& first = flatFirst ? flatMap : worldMap;
        const std::shared_ptr<const ArtistMap>& second = flatFirst ? worldMap : flatMap;
        const std::string* iso = first ? findArtist(*first, name) : nullptr;
        if (!iso && second) iso = findArtist(*second, name);
        return iso;
    }

    using MetaList = std::vector<std::pair<std::string, std::string>>;

    struct DynamicArtists {
        std::mutex m;
        metadb_handle_ptr track;
        std::vector<std::string> artists;
        MetaList meta;
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
            {
                std::lock_guard<std::mutex> lock(d.m);
                d.track = p_track;
                d.artists.clear();
                d.meta.clear();
            }
            ++g_generation;
        }
        void on_playback_stop(play_control::t_stop_reason) override {
            DynamicArtists& d = dynamicArtists();
            {
                std::lock_guard<std::mutex> lock(d.m);
                d.track.release();
                d.artists.clear();
                d.meta.clear();
            }
            ++g_generation;
        }
        void on_playback_dynamic_info_track(const file_info& p_info) override {
            std::vector<std::string> artists;
            MetaList meta;
            const t_size total = p_info.meta_get_count();
            for (t_size i = 0; i < total; ++i) {
                const t_size values = p_info.meta_enum_value_count(i);
                if (values == 0) continue;
                const std::string name = lower(p_info.meta_enum_name(i));
                meta.emplace_back(name, std::string(p_info.meta_enum_value(i, 0)));
                if (name == "artist")
                    for (t_size k = 0; k < values; ++k) artists.emplace_back(p_info.meta_enum_value(i, k));
            }
            DynamicArtists& d = dynamicArtists();
            {
                std::lock_guard<std::mutex> lock(d.m);
                d.artists = std::move(artists);
                d.meta = std::move(meta);
            }
            ++g_generation;
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

    bool dynamicOverlayFor(metadb_handle* h, MetaList& out) {
        DynamicArtists& d = dynamicArtists();
        std::lock_guard<std::mutex> lock(d.m);
        if (!d.track.is_valid() || d.track.get_ptr() != h) return false;
        out = d.meta;
        return true;
    }

    class dynamic_overlay_hook : public titleformat_hook {
    public:
        explicit dynamic_overlay_hook(MetaList items) : m_items(std::move(items)) {}

        bool process_field(titleformat_text_out* p_out, const char* p_name, t_size p_name_length, bool& p_found_flag) override {
            if (p_name_length == 9 && _strnicmp(p_name, "isplaying", 9) == 0) {
                p_out->write(titleformat_inputtypes::meta, "1", 1);
                p_found_flag = true;
                return true;
            }
            const std::string* v = find(std::string(p_name, p_name_length));
            if (!v) return false;
            p_out->write(titleformat_inputtypes::meta, v->c_str(), v->length());
            p_found_flag = true;
            return true;
        }

        bool process_function(titleformat_text_out* p_out, const char* p_name, t_size p_name_length,
            titleformat_hook_function_params* p_params, bool& p_found_flag) override {
            if (p_name_length != 4 || _strnicmp(p_name, "meta", 4) != 0) return false;
            if (p_params->get_param_count() < 1) return false;
            const char* field = nullptr;
            t_size fieldLength = 0;
            p_params->get_param(0, field, fieldLength);
            if (p_params->get_param_count() >= 2 && p_params->get_param_uint(1) != 0) return false;
            const std::string* v = find(std::string(field, fieldLength));
            if (!v) return false;
            p_out->write(titleformat_inputtypes::meta, v->c_str(), v->length());
            p_found_flag = true;
            return true;
        }

    private:
        const std::string* find(const std::string& name) const {
            const std::string key = lower(name);
            for (const auto& kv : m_items) if (kv.first == key) return &kv.second;
            return nullptr;
        }
        MetaList m_items;
    };

    std::vector<std::string> artistLookupCountries(metadb_handle* h) {
        std::vector<std::string> countries;
        const auto map = artistMap();
        const auto map2 = artistMap2();
        if ((!map || map->empty()) && (!map2 || map2->empty())) return countries;
        auto add = [&](const std::string& name) {
            const std::string* iso = findPreferred(map, map2, name);
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

    std::vector<std::string> splitOn(const std::vector<std::string>& parts, const std::string& sep) {
        std::vector<std::string> out;
        for (const auto& part : parts) {
            size_t pos = 0;
            for (;;) {
                const size_t hit = part.find(sep, pos);
                if (hit == std::string::npos) { out.push_back(part.substr(pos)); break; }
                out.push_back(part.substr(pos, hit - pos));
                pos = hit + sep.size();
            }
        }
        return out;
    }

    std::vector<std::string> lookupArtistName(const std::string& name) {
        std::vector<std::string> result;
        const auto map = artistMap();
        const auto map2 = artistMap2();
        auto find = [&](const std::string& n) -> const std::string* {
            return findPreferred(map, map2, n);
            };
        if (const std::string* iso = find(name)) { result.push_back(*iso); return result; }

        const std::string key = lookupKey(name);
        if (key.empty()) return result;
        std::vector<std::string> parts{ key };
        for (const char* sep : { " & ", " and ", " versus ", " vs. ", " vs ", " + ", ", ", "; ", " / ", " x " })
            parts = splitOn(parts, sep);
        for (const auto& part : parts) {
            if (part == key || part.empty()) continue;
            if (const std::string* iso = find(part)) { result.push_back(*iso); return result; }
        }
        return result;
    }

    struct ScriptCache {
        std::mutex m;
        std::string raw;
        titleformat_object::ptr obj;
        bool init = false;
    };

    ScriptCache& sourceScriptCache(size_t slot) {
        static ScriptCache* caches[10] = {
            new ScriptCache, new ScriptCache, new ScriptCache, new ScriptCache, new ScriptCache,
            new ScriptCache, new ScriptCache, new ScriptCache, new ScriptCache, new ScriptCache
        };
        return *caches[slot];
    }

    cfg_string* sourceConfig(size_t slot) {
        static cfg_string* configs[10] = {
            &g_cfgSource1, &g_cfgSource2, &g_cfgSource3, &g_cfgSource4, &g_cfgSource5,
            &g_cfgFallback1, &g_cfgFallback2, &g_cfgFallback3, &g_cfgFallback4, &g_cfgFallback5
        };
        return configs[slot];
    }

    titleformat_object::ptr compiledScript(size_t slot) {
        const char* src = sourceConfig(slot)->get();
        if (src == nullptr) src = "";

        ScriptCache& c = sourceScriptCache(slot);
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

    struct CountryCacheEntry {
        metadb_info_container::ptr info;
        unsigned generation = 0;
        std::chrono::steady_clock::time_point stored;
        std::vector<std::string> countries;
    };

    struct CountryCache {
        std::mutex m;
        std::unordered_map<std::uint64_t, CountryCacheEntry> map;
    };

    CountryCache& countryCache() {
        static CountryCache* c = new CountryCache;
        return *c;
    }

    class quit_cleanup : public initquit {
    public:
        void on_quit() override {
            for (size_t i = 0; i < 10; ++i) {
                ScriptCache& c = sourceScriptCache(i);
                std::lock_guard<std::mutex> lock(c.m);
                c.obj.release();
                c.init = false;
            }
            {
                CountryCache& cc = countryCache();
                std::lock_guard<std::mutex> lock(cc.m);
                cc.map.clear();
            }
            DynamicArtists& d = dynamicArtists();
            std::lock_guard<std::mutex> lock(d.m);
            d.track.release();
            d.artists.clear();
            d.meta.clear();
        }
    };
    initquit_factory_t<quit_cleanup> g_quit_cleanup;

    std::string evalScript(metadb_handle* h, size_t slot) {
        titleformat_object::ptr script = compiledScript(slot);
        if (script.is_empty()) return {};
        pfc::string8 result;
        MetaList overlay;
        if (dynamicOverlayFor(h, overlay)) {
            dynamic_overlay_hook hook(std::move(overlay));
            h->format_title(&hook, result, script, nullptr);
        }
        else {
            h->format_title(nullptr, result, script, nullptr);
        }
        return trim(result.c_str());
    }

    std::vector<std::string> parseCountryList(const std::string& whole) {
        std::vector<std::string> countries;
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

    std::vector<std::string> countriesOf(metadb_handle* h, size_t sourceIndex) {
        thread_local bool inside = false;
        if (inside) return {};
        struct Guard {
            bool& f;
            explicit Guard(bool& x) : f(x) { f = true; }
            ~Guard() { f = false; }
        } guard(inside);

        using clock = std::chrono::steady_clock;
        metadb_info_container::ptr info;
        h->get_info_ref(info);
        const unsigned generation = g_generation.load();
        const std::uint64_t key = (std::uint64_t)(std::uintptr_t)h | ((std::uint64_t)sourceIndex << 56);

        if (info.is_valid()) {
            CountryCache& cc = countryCache();
            std::lock_guard<std::mutex> lock(cc.m);
            const auto it = cc.map.find(key);
            if (it != cc.map.end() && it->second.generation == generation &&
                it->second.info.get_ptr() == info.get_ptr() &&
                clock::now() - it->second.stored < std::chrono::seconds(10))
                return it->second.countries;
        }

        std::vector<std::string> countries = parseCountryList(evalScript(h, sourceIndex));
        if (countries.empty()) {
            const std::string artist = evalScript(h, 5 + sourceIndex);
            if (!artist.empty()) countries = lookupArtistName(artist);
        }

        if (info.is_valid()) {
            CountryCache& cc = countryCache();
            std::lock_guard<std::mutex> lock(cc.m);
            if (cc.map.size() > 4096) cc.map.clear();
            CountryCacheEntry& e = cc.map[key];
            e.info = info;
            e.generation = generation;
            e.stored = clock::now();
            e.countries = countries;
        }
        return countries;
    }

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
    constexpr int ID_STATUS_FLAG = 1021;
    constexpr int ID_STATUS_ARTIST = 1022;
    constexpr int ID_STATUS_ARTIST2 = 1023;
    constexpr int ID_FALLBACK1 = 1031;
    constexpr int ID_FALLBACK2 = 1032;
    constexpr int ID_FALLBACK3 = 1033;
    constexpr int ID_FALLBACK4 = 1034;
    constexpr int ID_FALLBACK5 = 1035;
    constexpr int ID_ORDER_WORLD = 1041;
    constexpr int ID_ORDER_FLAT = 1042;
    constexpr int ID_TAB = 1050;
    constexpr const char* kFixedText = "(fixed - no suffix)";

    const int kEditIds[] = {
        ID_SOURCE1, ID_SOURCE2, ID_SOURCE3, ID_SOURCE4, ID_SOURCE5,
        ID_FALLBACK1, ID_FALLBACK2, ID_FALLBACK3, ID_FALLBACK4, ID_FALLBACK5,
        ID_FLAG_DIR, ID_ARTIST_FILE, ID_ARTIST2_FILE,
        ID_PREFIX1, ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5
    };

    static LRESULT CALLBACK TabChildWindowProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        return DefWindowProcW(wnd, msg, wp, lp);
    }

    class country_flags_preferences_instance : public preferences_page_instance {
    public:
        country_flags_preferences_instance(HWND parent, preferences_page_callback::ptr callback)
            : m_callback(callback), m_wnd(nullptr), m_tabWnd(nullptr), m_tab1Wnd(nullptr), m_tab2Wnd(nullptr),
            m_ready(false), m_originalOrder(0),
            m_note(nullptr), m_boldFont(nullptr),
            m_flagLabel(nullptr), m_artistLabel(nullptr), m_artist2Label(nullptr),
            m_orderLabel(nullptr), m_orderWorld(nullptr), m_orderFlat(nullptr),
            m_statusFlag(nullptr), m_statusArtist(nullptr), m_statusArtist2(nullptr),
            m_statusFont(nullptr), m_brDark(CreateSolidBrush(RGB(32, 32, 32))) {

            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES };
            InitCommonControlsEx(&icc);

            std::memset(m_rowLabel, 0, sizeof(m_rowLabel));
            std::memset(m_caption, 0, sizeof(m_caption));
            std::memset(m_statusColor, 0, sizeof(m_statusColor));

            HINSTANCE hInstance = core_api::get_my_instance();

            WNDCLASSW wc{};
            if (!GetClassInfoW(hInstance, L"CountryFlagsPreferencesPage", &wc)) {
                wc.lpfnWndProc = WindowProc;
                wc.hInstance = hInstance;
                wc.lpszClassName = L"CountryFlagsPreferencesPage";
                wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
                wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
                RegisterClassW(&wc);
            }

            WNDCLASSW wcTab{};
            if (!GetClassInfoW(hInstance, L"CountryFlagsTabChildPage", &wcTab)) {
                wcTab.lpfnWndProc = TabChildWindowProc;
                wcTab.hInstance = hInstance;
                wcTab.lpszClassName = L"CountryFlagsTabChildPage";
                wcTab.hCursor = LoadCursor(nullptr, IDC_ARROW);
                wcTab.hbrBackground = nullptr; // <-- Set to null so WM_ERASEBKGND handles the dark background
                RegisterClassW(&wcTab);
            }

            RECT rcParent{};
            GetClientRect(parent, &rcParent);
            int initW = (rcParent.right > 0) ? rcParent.right : 640;
            int initH = (rcParent.bottom > 0) ? rcParent.bottom : 580;

            m_wnd = CreateWindowExW(0, L"CountryFlagsPreferencesPage", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                0, 0, initW, initH, parent, nullptr, hInstance, this);

            if (m_wnd) {
                m_tabWnd = CreateWindowExW(0, WC_TABCONTROLW, L"",
                    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                    0, 0, initW, initH, m_wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TAB)),
                    hInstance, nullptr);
                SendMessageW(m_tabWnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

                SetWindowTheme(m_tabWnd, isDarkMode() ? L"DarkMode_Explorer" : L"", nullptr);
                SetWindowSubclass(m_tabWnd, TabControlSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

                TCITEMW tie{};
                tie.mask = TCIF_TEXT;
                tie.pszText = const_cast<LPWSTR>(L"Sources");
                TabCtrl_InsertItem(m_tabWnd, 0, &tie);
                tie.pszText = const_cast<LPWSTR>(L"Folders");
                TabCtrl_InsertItem(m_tabWnd, 1, &tie);

                RECT rcTab{};
                GetClientRect(m_tabWnd, &rcTab);
                TabCtrl_AdjustRect(m_tabWnd, FALSE, &rcTab);
                int tabW = rcTab.right - rcTab.left;
                int tabH = rcTab.bottom - rcTab.top;

                // Parent these to m_tabWnd instead of m_wnd
                m_tab1Wnd = CreateWindowExW(0, L"CountryFlagsTabChildPage", L"",
                    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                    rcTab.left, rcTab.top, tabW, tabH, m_tabWnd, nullptr, hInstance, this);

                m_tab2Wnd = CreateWindowExW(0, L"CountryFlagsTabChildPage", L"",
                    WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                    rcTab.left, rcTab.top, tabW, tabH, m_tabWnd, nullptr, hInstance, this);
                ShowWindow(m_tab2Wnd, SW_HIDE);
                createTab1Controls();
                createTab2Controls();

                loadValues(false);
                updateStatus();

                m_darkMode.AddDialogWithControls(m_wnd);
                m_tab1DarkMode.AddDialogWithControls(m_tab1Wnd);
                m_tab2DarkMode.AddDialogWithControls(m_tab2Wnd);
                m_ready = true;

                layoutControls(initW, initH);
            }
        }

        bool isDarkMode() const {
            // Query dark mode status directly from the foobar2000 dark mode helper object
            return m_darkMode;
        }

        ~country_flags_preferences_instance() {
            if (m_statusFont) DeleteObject(m_statusFont);
            if (m_boldFont) DeleteObject(m_boldFont);
            if (m_brDark) DeleteObject(m_brDark);
        }

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
            saveEdit(ID_FALLBACK1, g_cfgFallback1);
            saveEdit(ID_FALLBACK2, g_cfgFallback2);
            saveEdit(ID_FALLBACK3, g_cfgFallback3);
            saveEdit(ID_FALLBACK4, g_cfgFallback4);
            saveEdit(ID_FALLBACK5, g_cfgFallback5);
            saveEdit(ID_FLAG_DIR, g_cfgDir);
            saveEdit(ID_ARTIST_FILE, g_cfgArtistFile);
            saveEdit(ID_ARTIST2_FILE, g_cfgArtistFile2);
            g_cfgTableOrder = orderSelection();

            const int prefixIDs[] = { ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5 };
            for (size_t i = 0; i < 4; ++i) {
                const std::string suffix = cleanSuffix(readEdit(prefixIDs[i]), i + 1);
                pfc::stringcvt::string_wide_from_utf8 wide(suffix.c_str());
                SetWindowTextW(control(prefixIDs[i]), wide.get_ptr());
            }
            saveEdit(ID_PREFIX2, g_cfgPrefix2);
            saveEdit(ID_PREFIX3, g_cfgPrefix3);
            saveEdit(ID_PREFIX4, g_cfgPrefix4);
            saveEdit(ID_PREFIX5, g_cfgPrefix5);
            ++g_generation;
            captureOriginals();
            if (m_callback.is_valid()) m_callback->on_state_changed();
        }

        void reset() override {
            if (!m_wnd) return;
            setEdit(ID_SOURCE1, "%country%");
            setEdit(ID_SOURCE2, kDefaultSource2);
            setEdit(ID_SOURCE3, "");
            setEdit(ID_SOURCE4, "");
            setEdit(ID_SOURCE5, "");
            setEdit(ID_FALLBACK1, "");
            setEdit(ID_FALLBACK2, kDefaultFallback2);
            setEdit(ID_FALLBACK3, "");
            setEdit(ID_FALLBACK4, "");
            setEdit(ID_FALLBACK5, "");
            setEdit(ID_FLAG_DIR, "");
            setEdit(ID_ARTIST_FILE, "");
            setEdit(ID_ARTIST2_FILE, "");
            setOrder(0);
            setEdit(ID_PREFIX1, kFixedText);
            setEdit(ID_PREFIX2, "artist");
            setEdit(ID_PREFIX3, "3");
            setEdit(ID_PREFIX4, "4");
            setEdit(ID_PREFIX5, "5");
            if (m_callback.is_valid()) m_callback->on_state_changed();
        }

    private:
        static LRESULT CALLBACK TabChildWindowProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
            country_flags_preferences_instance* self =
                reinterpret_cast<country_flags_preferences_instance*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                self = static_cast<country_flags_preferences_instance*>(cs->lpCreateParams);
                SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            if (self) {
                if (msg == WM_CTLCOLORSTATIC && self->m_wnd) {
                    return SendMessageW(self->m_wnd, msg, wp, lp);
                }
                if (msg == WM_COMMAND && self->m_wnd) {
                    return SendMessageW(self->m_wnd, msg, wp, lp);
                }
                return self->handleMessage(wnd, msg, wp, lp);
            }
            return DefWindowProcW(wnd, msg, wp, lp);
        }

        static LRESULT CALLBACK TabControlSubclassProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR /*subclassId*/, DWORD_PTR refData) {
            country_flags_preferences_instance* self = reinterpret_cast<country_flags_preferences_instance*>(refData);
            if (self && self->isDarkMode()) {
                if (msg == WM_ERASEBKGND) {
                    HDC dc = (HDC)wp;
                    RECT rc{};
                    GetClientRect(wnd, &rc);
                    FillRect(dc, &rc, self->m_brDark);
                    return 1;
                }
            }
            return DefSubclassProc(wnd, msg, wp, lp);
        }

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
            bool dark = isDarkMode();

            if (msg == WM_ERASEBKGND) {
                HDC dc = (HDC)wp;
                RECT rc{};
                GetClientRect(wnd, &rc);
                HBRUSH br = dark ? m_brDark : GetSysColorBrush(COLOR_BTNFACE);
                FillRect(dc, &rc, br);
                return 1;
            }
            if (msg == WM_CTLCOLORSTATIC) {
                HDC dc = (HDC)wp;
                HWND ctrl = (HWND)lp;
                int id = GetDlgCtrlID(ctrl);
                if (id == ID_STATUS_FLAG || id == ID_STATUS_ARTIST || id == ID_STATUS_ARTIST2) {
                    int idx = (id == ID_STATUS_FLAG) ? 0 : (id == ID_STATUS_ARTIST2 ? 1 : 2);
                    SetTextColor(dc, m_statusColor[idx]);
                    SetBkMode(dc, TRANSPARENT);
                    return reinterpret_cast<INT_PTR>(dark ? m_brDark : GetSysColorBrush(COLOR_BTNFACE));
                }
                if (dark) {
                    SetTextColor(dc, RGB(240, 240, 240));
                    SetBkMode(dc, TRANSPARENT);
                    return reinterpret_cast<INT_PTR>(m_brDark);
                }
                SetBkMode(dc, TRANSPARENT);
                return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
            }

            // Forward commands and notifications from child tabs up to main preferences handler
            if ((wnd == m_tab1Wnd || wnd == m_tab2Wnd)) {
                if (msg == WM_COMMAND && m_wnd) {
                    return SendMessageW(m_wnd, msg, wp, lp);
                }
                return DefWindowProcW(wnd, msg, wp, lp);
            }

            if (wnd != m_wnd) {
                return DefWindowProcW(wnd, msg, wp, lp);
            }

            if (msg == WM_NOTIFY) {
                NMHDR* nm = reinterpret_cast<NMHDR*>(lp);
                if (nm && nm->idFrom == ID_TAB && nm->code == TCN_SELCHANGE) {
                    int sel = TabCtrl_GetCurSel(m_tabWnd);
                    ShowWindow(m_tab1Wnd, sel == 0 ? SW_SHOW : SW_HIDE);
                    ShowWindow(m_tab2Wnd, sel == 1 ? SW_SHOW : SW_HIDE);
                    return 0;
                }
            }
            if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED) {
                switch (LOWORD(wp)) {
                case ID_BROWSE: browseForFlagFolder(); return 0;
                case ID_ARTIST_BROWSE: browseForArtistFile(ID_ARTIST_FILE); return 0;
                case ID_ARTIST2_BROWSE: browseForArtistFile(ID_ARTIST2_FILE); return 0;
                case ID_ORDER_WORLD:
                case ID_ORDER_FLAT:
                    if (m_ready && m_callback.is_valid()) m_callback->on_state_changed();
                    return 0;
                default: break;
                }
            }
            if (msg == WM_COMMAND && HIWORD(wp) == EN_CHANGE) {
                if (m_ready) {
                    const int id = LOWORD(wp);
                    if (id == ID_FLAG_DIR || id == ID_ARTIST_FILE || id == ID_ARTIST2_FILE) updateStatus();
                    if (m_callback.is_valid()) m_callback->on_state_changed();
                }
            }
            if (msg == WM_SIZE) layoutControls(LOWORD(lp), HIWORD(lp));
            if (msg == WM_DPICHANGED) {
                layoutControls(GetClientRectWidth(wnd), GetClientRectHeight(wnd));
            }
            if (msg == WM_THEMECHANGED || msg == WM_SYSCOLORCHANGE || msg == WM_SETTINGCHANGE) {
                bool d = isDarkMode();
                if (m_tabWnd) SetWindowTheme(m_tabWnd, d ? L"DarkMode_Explorer" : L"", nullptr);
                InvalidateRect(m_wnd, nullptr, TRUE);
                if (m_tab1Wnd) InvalidateRect(m_tab1Wnd, nullptr, TRUE);
                if (m_tab2Wnd) InvalidateRect(m_tab2Wnd, nullptr, TRUE);
            }
            if (msg == WM_DESTROY) m_wnd = nullptr;
            return DefWindowProcW(wnd, msg, wp, lp);
        }

        static UINT dpiForWindow(HWND wnd) {
            using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
            static const auto getDpiForWindow = []() -> GetDpiForWindowFn {
                const HMODULE user32 = GetModuleHandleW(L"user32.dll");
                return user32 ? reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
                }();
            if (getDpiForWindow && wnd) {
                const UINT dpi = getDpiForWindow(wnd);
                if (dpi != 0) return dpi;
            }
            return 96;
        }

        int scale(int value) const { return MulDiv(value, static_cast<int>(dpiForWindow(m_wnd)), 96); }
        int GetClientRectWidth(HWND wnd) const { RECT r{}; GetClientRect(wnd, &r); return r.right; }
        int GetClientRectHeight(HWND wnd) const { RECT r{}; GetClientRect(wnd, &r); return r.bottom; }

        HWND control(int id) const {
            HWND c = GetDlgItem(m_tab1Wnd, id);
            if (c) return c;
            return GetDlgItem(m_tab2Wnd, id);
        }

        HWND addLabel(HWND parent, const wchar_t* text, int id = 0) const {
            HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                0, 0, 10, 10, parent, id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr,
                core_api::get_my_instance(), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return c;
        }

        HWND addEdit(HWND parent, int id) const {
            HWND c = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                core_api::get_my_instance(), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return c;
        }

        HWND addButton(HWND parent, int id) const {
            HWND c = CreateWindowExW(0, L"BUTTON", L"Browse...",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                core_api::get_my_instance(), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return c;
        }

        HWND addStatus(HWND parent, int id) const {
            HWND c = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
                0, 0, 10, 10, parent, id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr,
                core_api::get_my_instance(), nullptr);
            if (m_statusFont) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(m_statusFont), TRUE);
            return c;
        }

        void createTab1Controls() {
            {
                LOGFONTW lf{};
                GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
                lf.lfWeight = FW_BOLD;
                m_boldFont = CreateFontIndirectW(&lf);
            }

            const wchar_t* sourceTitles[] = { L"Source 1", L"Source 2", L"Source 3", L"Source 4", L"Source 5" };
            const wchar_t* captions[3] = { L"Suffix:", L"Titleformat", L"Fallback" };
            const int scriptIDs[] = { ID_SOURCE1, ID_SOURCE2, ID_SOURCE3, ID_SOURCE4, ID_SOURCE5 };
            const int fallbackIDs[] = { ID_FALLBACK1, ID_FALLBACK2, ID_FALLBACK3, ID_FALLBACK4, ID_FALLBACK5 };
            const int prefixIDs[] = { ID_PREFIX1, ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5 };
            for (int i = 0; i < 5; ++i) {
                m_rowLabel[i] = addLabel(m_tab1Wnd, sourceTitles[i]);
                if (m_boldFont) SendMessageW(m_rowLabel[i], WM_SETFONT, reinterpret_cast<WPARAM>(m_boldFont), TRUE);
                for (int c = 0; c < 3; ++c) m_caption[i][c] = addLabel(m_tab1Wnd, captions[c]);
                // created in visual order so Tab moves suffix -> titleformat -> fallback
                addEdit(m_tab1Wnd, prefixIDs[i]);
                addEdit(m_tab1Wnd, scriptIDs[i]);
                addEdit(m_tab1Wnd, fallbackIDs[i]);
            }
            setEdit(ID_PREFIX1, kFixedText);
            EnableWindow(control(ID_PREFIX1), FALSE);

            m_note = addLabel(m_tab1Wnd,
                L"Source 1 uses the fixed fields %countryflag_emoji%, %countryflag_path%, ...\n"
                L"Sources 2-5 use %countryflag_<suffix>_emoji% etc.: enter only the suffix, not \"countryflag_\".\n"
                L"Restart foobar2000 after changing a suffix.\n"
                L"The fallback script runs only when the script returns no country; its result (an artist name)\n"
                L"is looked up in the artist tables on the Folders tab.");
        }

        void createTab2Controls() {
            m_statusFont = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI Symbol");

            m_flagLabel = addLabel(m_tab2Wnd, L"Flag image folder (empty = component's flags folder)");
            m_statusFlag = addStatus(m_tab2Wnd, ID_STATUS_FLAG);
            addEdit(m_tab2Wnd, ID_FLAG_DIR);
            addButton(m_tab2Wnd, ID_BROWSE);

            m_artist2Label = addLabel(m_tab2Wnd, L"Artist table worldMap.json: last item of val = country (empty = profile\\js_data\\worldMap.json, else next to the component)");
            m_statusArtist2 = addStatus(m_tab2Wnd, ID_STATUS_ARTIST2);
            addEdit(m_tab2Wnd, ID_ARTIST2_FILE);
            addButton(m_tab2Wnd, ID_ARTIST2_BROWSE);

            m_artistLabel = addLabel(m_tab2Wnd, L"Artist table country_codes.json: {\"artist\": \"CC\"} (empty = next to the component)");
            m_statusArtist = addStatus(m_tab2Wnd, ID_STATUS_ARTIST);
            addEdit(m_tab2Wnd, ID_ARTIST_FILE);
            addButton(m_tab2Wnd, ID_ARTIST_BROWSE);

            m_orderLabel = addLabel(m_tab2Wnd, L"When an artist is in both tables, use the country from:");
            m_orderWorld = CreateWindowExW(0, L"BUTTON", L"worldMap.json (overrides country_codes.json)",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
                0, 0, 10, 10, m_tab2Wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ORDER_WORLD)),
                core_api::get_my_instance(), nullptr);
            m_orderFlat = CreateWindowExW(0, L"BUTTON", L"country_codes.json (overrides worldMap.json)",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                0, 0, 10, 10, m_tab2Wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ORDER_FLAT)),
                core_api::get_my_instance(), nullptr);
            SendMessageW(m_orderWorld, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            SendMessageW(m_orderFlat, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        }

        void layoutControls(int width, int height) {
            if (!m_wnd || !m_tabWnd || !m_tab1Wnd) return;
            auto S = [&](int v) { return scale(v); };
            const int margin = S(12), gap = S(8), rowH = S(26), browseW = S(86), statusW = S(20);

            SetWindowPos(m_tabWnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);

            RECT rcTab{ 0, 0, width, height };
            TabCtrl_AdjustRect(m_tabWnd, FALSE, &rcTab);
            const int tabW = (std::max)(S(100), static_cast<int>(rcTab.right - rcTab.left));
            const int tabH = (std::max)(S(100), static_cast<int>(rcTab.bottom - rcTab.top));

            if (m_tab1Wnd) SetWindowPos(m_tab1Wnd, nullptr, rcTab.left, rcTab.top, tabW, tabH, SWP_NOZORDER | SWP_NOACTIVATE);
            if (m_tab2Wnd) SetWindowPos(m_tab2Wnd, nullptr, rcTab.left, rcTab.top, tabW, tabH, SWP_NOZORDER | SWP_NOACTIVATE);

            // --- Layout Tab 1 (Sources): no scrolling; everything is sized to fit the page ---
            // Per source:  [Source N]  Suffix: [edit]
            //              Titleformat [edit]
            //              Fallback    [edit]
            const int nominalRowH = 24, nominalGap = 4, nominalGroupGap = 10, nominalNoteH = 84, nominalTop = 12;
            const int nominalH = nominalTop + 5 * (3 * nominalRowH + 2 * nominalGap + nominalGroupGap) + nominalNoteH + nominalTop;
            // Shrink vertical spacing (never below 75%) when the page is shorter than the nominal layout.
            const int pct = (std::max)(75, (std::min)(100, MulDiv(tabH, 100, S(nominalH))));
            auto V = [&](int v) { return MulDiv(S(v), pct, 100); };
            const int rowH1 = V(nominalRowH), rowGap = V(nominalGap), groupGap = V(nominalGroupGap), noteH = V(nominalNoteH);

            const int fullW1 = (std::max)(S(100), tabW - 2 * margin);
            const int captionW = S(84);
            const int xEdit = margin + captionW;
            const int editW = (std::max)(S(60), fullW1 - captionW);
            const int suffixCapW = S(46), suffixEditW = (std::min)(S(200), (std::max)(S(60), editW - suffixCapW - gap));

            auto place = [&](HWND h, int x, int y, int w, int hgt) {
                if (h) SetWindowPos(h, nullptr, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
                };
            const int capH = S(18);
            const int capDy = (rowH1 - capH) / 2 + S(1);

            const int scriptIDs[] = { ID_SOURCE1, ID_SOURCE2, ID_SOURCE3, ID_SOURCE4, ID_SOURCE5 };
            const int fallbackIDs[] = { ID_FALLBACK1, ID_FALLBACK2, ID_FALLBACK3, ID_FALLBACK4, ID_FALLBACK5 };
            const int prefixIDs[] = { ID_PREFIX1, ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5 };
            int y1 = V(nominalTop);
            for (int i = 0; i < 5; ++i) {
                // Row 1: source name + suffix
                place(m_rowLabel[i], margin, y1 + capDy, captionW - gap, capH);
                place(m_caption[i][0], xEdit, y1 + capDy, suffixCapW, capH);
                place(GetDlgItem(m_tab1Wnd, prefixIDs[i]), xEdit + suffixCapW + gap, y1, suffixEditW, rowH1);
                y1 += rowH1 + rowGap;
                // Row 2: titleformat
                place(m_caption[i][1], margin, y1 + capDy, captionW - gap, capH);
                place(GetDlgItem(m_tab1Wnd, scriptIDs[i]), xEdit, y1, editW, rowH1);
                y1 += rowH1 + rowGap;
                // Row 3: fallback
                place(m_caption[i][2], margin, y1 + capDy, captionW - gap, capH);
                place(GetDlgItem(m_tab1Wnd, fallbackIDs[i]), xEdit, y1, editW, rowH1);
                y1 += rowH1 + groupGap;
            }
            place(m_note, margin, y1, fullW1, noteH);

            // --- Layout Tab 2 (Folders) ---
            const int fullW2 = (std::max)(S(100), tabW - 2 * margin);
            int y2 = S(12);
            if (m_flagLabel) SetWindowPos(m_flagLabel, nullptr, margin, y2, fullW2, S(18), SWP_NOZORDER | SWP_NOACTIVATE); y2 += S(21);

            auto pathRow = [&](HWND tabParent, HWND status, int editId, int browseId) {
                if (!status) return;
                const int actualStatusW = (std::min)(statusW, (std::max)(S(12), fullW2 / 12));
                const int actualBrowseW = (std::min)(browseW, (std::max)(S(52), fullW2 / 4));
                const int editW = (std::max)(S(20), fullW2 - actualStatusW - actualBrowseW - 3 * gap);
                SetWindowPos(status, nullptr, margin, y2 + S(3), actualStatusW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
                HWND hwEdit = GetDlgItem(tabParent, editId);
                HWND hwBrowse = GetDlgItem(tabParent, browseId);
                if (hwEdit) SetWindowPos(hwEdit, nullptr, margin + actualStatusW + gap, y2, editW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
                if (hwBrowse) SetWindowPos(hwBrowse, nullptr, margin + actualStatusW + gap + editW + gap, y2, actualBrowseW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
                y2 += rowH + S(12);
                };

            pathRow(m_tab2Wnd, m_statusFlag, ID_FLAG_DIR, ID_BROWSE);
            if (m_artist2Label) SetWindowPos(m_artist2Label, nullptr, margin, y2, fullW2, S(34), SWP_NOZORDER | SWP_NOACTIVATE); y2 += S(37);
            pathRow(m_tab2Wnd, m_statusArtist2, ID_ARTIST2_FILE, ID_ARTIST2_BROWSE);
            if (m_artistLabel) SetWindowPos(m_artistLabel, nullptr, margin, y2, fullW2, S(30), SWP_NOZORDER | SWP_NOACTIVATE); y2 += S(33);
            pathRow(m_tab2Wnd, m_statusArtist, ID_ARTIST_FILE, ID_ARTIST_BROWSE);
            if (m_orderLabel) SetWindowPos(m_orderLabel, nullptr, margin, y2, fullW2, S(18), SWP_NOZORDER | SWP_NOACTIVATE); y2 += S(22);
            if (m_orderWorld) SetWindowPos(m_orderWorld, nullptr, margin + S(8), y2, fullW2 - S(8), S(22), SWP_NOZORDER | SWP_NOACTIVATE); y2 += S(25);
            if (m_orderFlat) SetWindowPos(m_orderFlat, nullptr, margin + S(8), y2, fullW2 - S(8), S(22), SWP_NOZORDER | SWP_NOACTIVATE);
        }

        void updateStatus() {
            if (!m_wnd) return;
            struct Item { int edit; int status; bool folder; const char* defaultName; bool profileJsData; };
            static const Item items[3] = {
                { ID_FLAG_DIR, ID_STATUS_FLAG, true, "flags", false },
                { ID_ARTIST2_FILE, ID_STATUS_ARTIST2, false, "worldMap.json", true },
                { ID_ARTIST_FILE, ID_STATUS_ARTIST, false, "country_codes.json", false },
            };
            for (int i = 0; i < 3; ++i) {
                const std::string text = trim(readEdit(items[i].edit).c_str());
                const fs::path p = text.empty() ? defaultTablePath(items[i].defaultName, items[i].profileJsData) : expandPath(text);
                std::error_code ec;
                const bool ok = items[i].folder ? fs::is_directory(p, ec) : fs::is_regular_file(p, ec);

                // Set green for valid (check), red for invalid (cross), gray if empty
                m_statusColor[i] = ok ? RGB(46, 170, 70) : (text.empty() ? RGB(128, 128, 128) : RGB(215, 60, 60));

                HWND st = control(items[i].status);
                SetWindowTextW(st, ok ? L"\u2713" : L"\u2717");
                InvalidateRect(st, nullptr, TRUE);
            }
        }

        void browseForFlagFolder() const {
            IFileOpenDialog* dlg = nullptr;
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg)
                return;
            DWORD opts = 0;
            dlg->GetOptions(&opts);
            dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            dlg->SetTitle(L"Select flag image folder");

            const std::string current = trim(readEdit(ID_FLAG_DIR).c_str());
            if (!current.empty()) {
                const std::wstring wcur = expandPath(current).wstring();
                IShellItem* folder = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(wcur.c_str(), nullptr, IID_PPV_ARGS(&folder))) && folder) {
                    dlg->SetFolder(folder);
                    folder->Release();
                }
            }

            if (SUCCEEDED(dlg->Show(m_wnd))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                        SetWindowTextW(control(ID_FLAG_DIR), portablePath(path).c_str());
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dlg->Release();
        }

        void browseForArtistFile(int editId) const {
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

            const std::string current = trim(readEdit(editId).c_str());
            if (!current.empty()) {
                const fs::path cur = expandPath(current);
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
                        SetWindowTextW(control(editId), portablePath(path).c_str());
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dlg->Release();
        }

        void setOrder(int value) const {
            if (control(ID_ORDER_WORLD)) SendMessageW(control(ID_ORDER_WORLD), BM_SETCHECK,
                value == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
            if (control(ID_ORDER_FLAT)) SendMessageW(control(ID_ORDER_FLAT), BM_SETCHECK,
                value == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
        }

        int orderSelection() const {
            return control(ID_ORDER_FLAT) &&
                SendMessageW(control(ID_ORDER_FLAT), BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
        }

        std::string readEdit(int id) const {
            HWND edit = control(id);
            if (!edit) return {};
            const int len = GetWindowTextLengthW(edit);
            std::wstring wide(static_cast<size_t>(len) + 1, L'\0');
            GetWindowTextW(edit, wide.data(), len + 1);
            wide.resize(len);
            pfc::stringcvt::string_utf8_from_wide utf8(wide.c_str());
            return std::string(utf8.get_ptr());
        }

        void setEdit(int id, const char* value) const {
            pfc::stringcvt::string_wide_from_utf8 wide(value);
            HWND edit = control(id);
            if (edit) SetWindowTextW(edit, wide.get_ptr());
        }

        void saveEdit(int id, cfg_string& cfg) const {
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
            setEdit(ID_FALLBACK1, g_cfgFallback1.get());
            setEdit(ID_FALLBACK2, g_cfgFallback2.get());
            setEdit(ID_FALLBACK3, g_cfgFallback3.get());
            setEdit(ID_FALLBACK4, g_cfgFallback4.get());
            setEdit(ID_FALLBACK5, g_cfgFallback5.get());
            setEdit(ID_FLAG_DIR, g_cfgDir.get());
            setEdit(ID_ARTIST_FILE, g_cfgArtistFile.get());
            setEdit(ID_ARTIST2_FILE, g_cfgArtistFile2.get());
            setOrder(g_cfgTableOrder.get_value() == 1 ? 1 : 0);
            setEdit(ID_PREFIX1, kFixedText);
            setEdit(ID_PREFIX2, cleanSuffix(trim(g_cfgPrefix2.get()), 1).c_str());
            setEdit(ID_PREFIX3, cleanSuffix(trim(g_cfgPrefix3.get()), 2).c_str());
            setEdit(ID_PREFIX4, cleanSuffix(trim(g_cfgPrefix4.get()), 3).c_str());
            setEdit(ID_PREFIX5, cleanSuffix(trim(g_cfgPrefix5.get()), 4).c_str());
            captureOriginals();
        }

        void captureOriginals() {
            for (int id : kEditIds) m_original[id] = readEdit(id);
            m_originalOrder = orderSelection();
        }

        bool hasChanges() const {
            if (orderSelection() != m_originalOrder) return true;
            for (int id : kEditIds) {
                const auto it = m_original.find(id);
                if (it == m_original.end() || it->second != readEdit(id)) return true;
            }
            return false;
        }

        HWND m_wnd;
        HWND m_tabWnd;
        HWND m_tab1Wnd;
        HWND m_tab2Wnd;
        bool m_ready;
        int m_originalOrder;
        HWND m_note;
        HWND m_caption[5][3];   // per source: suffix / titleformat / fallback captions
        HFONT m_boldFont;
        HWND m_flagLabel, m_artistLabel, m_artist2Label;
        HWND m_orderLabel, m_orderWorld, m_orderFlat;
        HWND m_statusFlag, m_statusArtist, m_statusArtist2;
        HWND m_rowLabel[5];
        HFONT m_statusFont;
        HBRUSH m_brDark;
        COLORREF m_statusColor[3];
        fb2k::CCoreDarkModeHooks m_darkMode;
        fb2k::CCoreDarkModeHooks m_tab1DarkMode;
        fb2k::CCoreDarkModeHooks m_tab2DarkMode;
        preferences_page_callback::ptr m_callback;
        std::unordered_map<int, std::string> m_original;
    };

    class country_flags_preferences_page : public preferences_page_v3 {
    public:
        const char* get_name() override { return "Country Flags"; }
        GUID get_guid() override { return guid_country_flags_preferences; }
        GUID get_parent_guid() override { return preferences_page::guid_tools; }
        preferences_page_instance::ptr instantiate(fb2k::hwnd_t parent, preferences_page_callback::ptr callback) override {
            return new service_impl_t<country_flags_preferences_instance>(reinterpret_cast<HWND>(parent), callback);
        }
    };
    preferences_page_factory_t<country_flags_preferences_page> g_country_flags_preferences_page;

    class flag_fields : public metadb_display_field_provider {
    public:
        static constexpr t_uint32 kKinds = 7;
        static constexpr t_uint32 kLookupIndex = 5 * kKinds;

        t_uint32 get_field_count() override { return kLookupIndex + 1; }

        void get_field_name(t_uint32 index, pfc::string_base& out) override {
            if (index == kLookupIndex) { out.set_string("countryflag_lookup"); return; }
            if (index > kLookupIndex) { out.reset(); return; }
            static const char* suffixes[kKinds] = { "emoji", "path", "name", "iso2", "iso3", "region", "subregion" };
            const std::string prefix = fieldPrefix(index / kKinds);
            if (prefix.empty()) { out.set_string(""); return; }
            const std::string name = prefix + suffixes[index % kKinds];
            out.set_string(name.c_str());
        }

        bool process_field(t_uint32 index, metadb_handle* handle, titleformat_text_out* out) override {
            if (index == kLookupIndex) {
                std::string value;
                for (const auto& iso : artistLookupCountries(handle)) {
                    if (!value.empty()) value += "; ";
                    value += iso;
                }
                if (value.empty()) return false;
                out->write(titleformat_inputtypes::meta, value.c_str(), value.length());
                return true;
            }
            const size_t sourceIndex = index / kKinds;
            const t_uint32 kind = index % kKinds;
            const std::vector<std::string> countries = countriesOf(handle, sourceIndex);
            if (countries.empty()) return false;

            std::string value;
            std::vector<std::string> seen;
            const char* separator = kind == 0 ? " " : "; ";
            for (const auto& iso : countries) {
                std::string part;
                switch (kind) {
                case 0: part = emojiFlag(iso); break;
                case 1: part = flagPath(iso); break;
                case 2: { auto it = db().a2name.find(iso); if (it != db().a2name.end()) part = it->second; break; }
                case 3: part = iso; break;
                case 4: { auto it = db().a2to3.find(iso); if (it != db().a2to3.end()) part = it->second; break; }
                case 5:
                case 6: {
                    auto it = db().a2region.find(iso);
                    if (it == db().a2region.end()) break;
                    for (const auto& rs : it->second) {
                        const std::string& v = kind == 5 ? rs.first : rs.second;
                        if (v.empty() || std::find(seen.begin(), seen.end(), v) != seen.end()) continue;
                        seen.push_back(v);
                        if (!value.empty()) value += separator;
                        value += v;
                    }
                    continue;
                }
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