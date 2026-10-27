// foo_countryflags - exposes country flag fields to title formatting.
//
// Fields (usable anywhere titleformatting works, e.g. %countryflag_path%):
//   %countryflag%        flag emoji built from regional indicators (e.g. GR -> 🇬🇷)
//   %countryflag_path%   full path to <flagdir>\<iso2>.png (only if the file exists)
//   %countryflag_name%   English country name
//   %countryflag_iso2%   normalised ISO 3166-1 alpha-2 code, upper case
//   %countryflag_iso3%   matching ISO 3166-1 alpha-3 code, upper case (e.g. GRC)
//
// The country is taken from the first tag in the configured list that holds a
// value we can recognise (alpha-2, alpha-3 or English name).
// Preferences > Advanced > Tools > Country flags.
//
// Data file: countries.txt next to the DLL, lines of ISO2|ISO3|Name
// Flag images: flags\gr.png, flags\us.png, ... next to the DLL (or custom folder).
//
// Build: VS2022, x64, C++17, link foobar2000_SDK, foobar2000_component_client, pfc.

#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/helpers/helpers.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

DECLARE_COMPONENT_VERSION("Country Flags", "0.1.0",
    "Maps country codes/names in tags to flags for title formatting.");
VALIDATE_COMPONENT_FILENAME("foo_countryflags.dll");

namespace {

namespace fs = std::filesystem;

    const GUID guid_branch = { 0x916ca10, 0x4e9f, 0x45ca, { 0xad, 0x87, 0xc5, 0x4f, 0x1e, 0xcb, 0x8f, 0x46 } };
    const GUID guid_src = { 0xafdb315d, 0x3d2d, 0x4add, { 0x81, 0xf0, 0x55, 0xbb, 0xeb, 0x27, 0xd9, 0x9d } };
    const GUID guid_dir = { 0xb46e52be, 0x3476, 0x45b5, { 0x98, 0x4f, 0xff, 0x11, 0xbd, 0x1f, 0x98, 0x8b } };
											

advconfig_branch_factory g_branch("Country flags", guid_branch, advconfig_entry::guid_branch_tools, 0.0);
advconfig_string_factory_MT g_cfgSrc("Source tag fields (semicolon-separated, first match wins)",
    guid_src, guid_branch, 0.0, "COUNTRYCODE;COUNTRY;ARTISTCOUNTRY;RELEASECOUNTRY");
advconfig_string_factory_MT g_cfgDir("Flag image folder (empty = 'flags' next to component)",
    guid_dir, guid_branch, 1.0, "");

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
        // common aliases
        const char* aliases[][2] = {
            {"united states of america", "US"}, {"czech republic", "CZ"}, {"turkiye", "TR"},
            {"türkiye", "TR"}, {"burma", "MM"}, {"ivory coast", "CI"}, {"cape verde", "CV"},
            {"vatican", "VA"}, {"macedonia", "MK"}, {"swaziland", "SZ"}, {"east timor", "TL"},
            {"russian federation", "RU"}, {"korea", "KR"}, {"great britain", "GB"},
            {"england", "GB"}, {"scotland", "GB"}, {"wales", "GB"}, {"uk", "GB"},
            {"holland", "NL"}, {"hellas", "GR"}
        };
        for (auto& a : aliases) d.name2to2[a[0]] = a[1];
    });
    return d;
}

// Returns upper-case ISO2, or "" if unrecognised.
std::string normalize(const char* raw) {
    std::string s = trim(raw);
    if (s.empty()) return {};
    const Db& d = db();
    std::string up = upper(s);
    if (up.size() == 2 && d.a2name.count(up)) return up;
    if (up.size() == 3) {
        auto it = d.a3to2.find(up);
        if (it != d.a3to2.end()) return it->second;
    }
    auto it = d.name2to2.find(lower(s));
    return it != d.name2to2.end() ? it->second : std::string();
}

std::string emojiFlag(const std::string& iso2) {
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
    g_cfgDir.get(custom);
    const std::string customDir = trim(custom.c_str());

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
											   
    fs::path p = dir / (lower(iso2) + ".png");
    std::error_code ec;
    std::string result = fs::exists(p, ec) ? toUtf8(p) : std::string();
    cache[iso2] = Entry{ result, now };
    return result;
}

// Parsed source tag list. Re-parsed only when the setting string changes; callers
// get a shared, immutable snapshot so no copying or locking happens while iterating.
std::shared_ptr<const std::vector<std::string>> sourceFields() {
    static std::mutex m;
    static std::string lastRaw;
    static std::shared_ptr<const std::vector<std::string>> parsed;

    pfc::string8 src;
    g_cfgSrc.get(src);

    std::lock_guard<std::mutex> lock(m);
    if (!parsed || lastRaw != src.c_str()) {
        auto v = std::make_shared<std::vector<std::string>>();
        const std::string list = src.c_str();
        size_t pos = 0;
        while (pos <= list.size()) {
            size_t end = list.find(';', pos);
            if (end == std::string::npos) end = list.size();
            std::string field = trim(list.substr(pos, end - pos).c_str());
            pos = end + 1;
            if (!field.empty()) v->push_back(std::move(field));
        }
        lastRaw = src.c_str();
        parsed = std::move(v);
    }
    return parsed;
}

// Find the country for a track using the configured tag list.
std::string countryOf(metadb_handle* h) {
    metadb_info_container::ptr c;
    if (!h->get_info_ref(c)) return {};
    const file_info& fi = c->info();

    const auto fields = sourceFields();
    for (const std::string& field : *fields) {
        const size_t n = fi.meta_get_count_by_name(field.c_str());
        for (size_t i = 0; i < n; ++i) {
            std::string iso = normalize(fi.meta_get(field.c_str(), i));
            if (!iso.empty()) return iso;
        }
    }
    return {};
}

// --- field provider -------------------------------------------------------

class flag_fields : public metadb_display_field_provider {
public:
    t_uint32 get_field_count() override { return 5; }

    void get_field_name(t_uint32 index, pfc::string_base& out) override {
        static const char* names[] = { "countryflag_emoji", "countryflag_path",
                                       "countryflag_name", "countryflag_iso2",
                                       "countryflag_iso3" };
        out.set_string(names[index]);
    }

    bool process_field(t_uint32 index, metadb_handle* handle, titleformat_text_out* out) override {
        const std::string iso = countryOf(handle);
        if (iso.empty()) return false;

        std::string value;
        switch (index) {
        case 0: value = emojiFlag(iso); break;
        case 1: value = flagPath(iso); break;
        case 2: { auto it = db().a2name.find(iso); if (it != db().a2name.end()) value = it->second; break; }
        case 3: value = iso; break;
        case 4: { auto it = db().a2to3.find(iso); if (it != db().a2to3.end()) value = it->second; break; }
        }
        if (value.empty()) return false;
        out->write(titleformat_inputtypes::meta, value.c_str(), value.length());
        return true;
    }
};

service_factory_single_t<flag_fields> g_flag_fields;

} // namespace
