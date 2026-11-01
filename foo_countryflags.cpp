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
//   (country_codes.json: flat JSON {"artist name": "CC"}, case-insensitive, reloaded when changed).
//   For the playing stream, the dynamic (ICY) artist is searched first.
//   The file locations can be set in preferences. Empty box = default: country_codes.json is
//   profile\foo_uie_jsplitter\package_data\{BA9557CE-7B4B-4E0E-9373-99F511E81252}\country_codes.json
//   if that file exists, else next to the DLL; worldMap.json is profile\js_data\worldMap.json,
//   else next to the DLL.
//   worldMap.json ([{"artist": "...", "val": [..., "Country"]}]; the last item of val is the
//   country) and country_codes.json are both searched; which one takes precedence when an
//   artist is in both is a setting (default: worldMap.json first).
// Preferences > Tools > Country Flags.
// The script must not use the per-source %countryflag_*% fields (re-entrant calls are
// ignored to prevent infinite recursion). %countryflag_lookup% is safe to use in scripts.
//
// Countries: a built-in ISO 3166-1 table. An optional countries.txt next to the DLL (lines of
// ISO2|ISO3|Name) overrides entries with the same ISO2 and can add more.
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
#include <cstdio>
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
#include <commdlg.h>
#include <uxtheme.h>
#include <shobjidl.h>
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")

DECLARE_COMPONENT_VERSION("Country Flags", "0.1.6",
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

    // Built-in ISO 3166-1 table (ISO2, ISO3, English name). An optional countries.txt next to the DLL
    // (lines ISO2|ISO3|Name) overrides entries with the same ISO2 and can add more.
    struct CountryRow { const char* a2; const char* a3; const char* name; };

    const CountryRow kBuiltinCountries[] = {
        { "AF", "AFG", "Afghanistan" },
        { "AX", "ALA", "\xC3\x85land Islands" },
        { "AL", "ALB", "Albania" },
        { "DZ", "DZA", "Algeria" },
        { "AS", "ASM", "American Samoa" },
        { "AD", "AND", "Andorra" },
        { "AO", "AGO", "Angola" },
        { "AI", "AIA", "Anguilla" },
        { "AQ", "ATA", "Antarctica" },
        { "AG", "ATG", "Antigua and Barbuda" },
        { "AR", "ARG", "Argentina" },
        { "AM", "ARM", "Armenia" },
        { "AW", "ABW", "Aruba" },
        { "AU", "AUS", "Australia" },
        { "AT", "AUT", "Austria" },
        { "AZ", "AZE", "Azerbaijan" },
        { "BS", "BHS", "Bahamas" },
        { "BH", "BHR", "Bahrain" },
        { "BD", "BGD", "Bangladesh" },
        { "BB", "BRB", "Barbados" },
        { "BY", "BLR", "Belarus" },
        { "BE", "BEL", "Belgium" },
        { "BZ", "BLZ", "Belize" },
        { "BJ", "BEN", "Benin" },
        { "BM", "BMU", "Bermuda" },
        { "BT", "BTN", "Bhutan" },
        { "BO", "BOL", "Bolivia" },
        { "BQ", "BES", "Bonaire, Sint Eustatius and Saba" },
        { "BA", "BIH", "Bosnia and Herzegovina" },
        { "BW", "BWA", "Botswana" },
        { "BV", "BVT", "Bouvet Island" },
        { "BR", "BRA", "Brazil" },
        { "IO", "IOT", "British Indian Ocean Territory" },
        { "VG", "VGB", "British Virgin Islands" },
        { "BN", "BRN", "Brunei" },
        { "BG", "BGR", "Bulgaria" },
        { "BF", "BFA", "Burkina Faso" },
        { "BI", "BDI", "Burundi" },
        { "CV", "CPV", "Cabo Verde" },
        { "KH", "KHM", "Cambodia" },
        { "CM", "CMR", "Cameroon" },
        { "CA", "CAN", "Canada" },
        { "KY", "CYM", "Cayman Islands" },
        { "CF", "CAF", "Central African Republic" },
        { "TD", "TCD", "Chad" },
        { "CL", "CHL", "Chile" },
        { "CN", "CHN", "China" },
        { "CX", "CXR", "Christmas Island" },
        { "CC", "CCK", "Cocos (Keeling) Islands" },
        { "CO", "COL", "Colombia" },
        { "KM", "COM", "Comoros" },
        { "CG", "COG", "Congo" },
        { "CD", "COD", "Democratic Republic of the Congo" },
        { "CK", "COK", "Cook Islands" },
        { "CR", "CRI", "Costa Rica" },
        { "CI", "CIV", "C\xC3\xB4te d'Ivoire" },
        { "HR", "HRV", "Croatia" },
        { "CU", "CUB", "Cuba" },
        { "CW", "CUW", "Cura\xC3\xA7" "ao" },
        { "CY", "CYP", "Cyprus" },
        { "CZ", "CZE", "Czechia" },
        { "DK", "DNK", "Denmark" },
        { "DJ", "DJI", "Djibouti" },
        { "DM", "DMA", "Dominica" },
        { "DO", "DOM", "Dominican Republic" },
        { "EC", "ECU", "Ecuador" },
        { "EG", "EGY", "Egypt" },
        { "SV", "SLV", "El Salvador" },
        { "GQ", "GNQ", "Equatorial Guinea" },
        { "ER", "ERI", "Eritrea" },
        { "EE", "EST", "Estonia" },
        { "SZ", "SWZ", "Eswatini" },
        { "ET", "ETH", "Ethiopia" },
        { "FK", "FLK", "Falkland Islands" },
        { "FO", "FRO", "Faroe Islands" },
        { "FJ", "FJI", "Fiji" },
        { "FI", "FIN", "Finland" },
        { "FR", "FRA", "France" },
        { "GF", "GUF", "French Guiana" },
        { "PF", "PYF", "French Polynesia" },
        { "TF", "ATF", "French Southern Territories" },
        { "GA", "GAB", "Gabon" },
        { "GM", "GMB", "Gambia" },
        { "GE", "GEO", "Georgia" },
        { "DE", "DEU", "Germany" },
        { "GH", "GHA", "Ghana" },
        { "GI", "GIB", "Gibraltar" },
        { "GR", "GRC", "Greece" },
        { "GL", "GRL", "Greenland" },
        { "GD", "GRD", "Grenada" },
        { "GP", "GLP", "Guadeloupe" },
        { "GU", "GUM", "Guam" },
        { "GT", "GTM", "Guatemala" },
        { "GG", "GGY", "Guernsey" },
        { "GN", "GIN", "Guinea" },
        { "GW", "GNB", "Guinea-Bissau" },
        { "GY", "GUY", "Guyana" },
        { "HT", "HTI", "Haiti" },
        { "HM", "HMD", "Heard Island and McDonald Islands" },
        { "VA", "VAT", "Vatican City" },
        { "HN", "HND", "Honduras" },
        { "HK", "HKG", "Hong Kong" },
        { "HU", "HUN", "Hungary" },
        { "IS", "ISL", "Iceland" },
        { "IN", "IND", "India" },
        { "ID", "IDN", "Indonesia" },
        { "IR", "IRN", "Iran" },
        { "IQ", "IRQ", "Iraq" },
        { "IE", "IRL", "Ireland" },
        { "IM", "IMN", "Isle of Man" },
        { "IL", "ISR", "Israel" },
        { "IT", "ITA", "Italy" },
        { "JM", "JAM", "Jamaica" },
        { "JP", "JPN", "Japan" },
        { "JE", "JEY", "Jersey" },
        { "JO", "JOR", "Jordan" },
        { "KZ", "KAZ", "Kazakhstan" },
        { "KE", "KEN", "Kenya" },
        { "KI", "KIR", "Kiribati" },
        { "XK", "XKX", "Kosovo" },
        { "KW", "KWT", "Kuwait" },
        { "KG", "KGZ", "Kyrgyzstan" },
        { "LA", "LAO", "Laos" },
        { "LV", "LVA", "Latvia" },
        { "LB", "LBN", "Lebanon" },
        { "LS", "LSO", "Lesotho" },
        { "LR", "LBR", "Liberia" },
        { "LY", "LBY", "Libya" },
        { "LI", "LIE", "Liechtenstein" },
        { "LT", "LTU", "Lithuania" },
        { "LU", "LUX", "Luxembourg" },
        { "MO", "MAC", "Macao" },
        { "MG", "MDG", "Madagascar" },
        { "MW", "MWI", "Malawi" },
        { "MY", "MYS", "Malaysia" },
        { "MV", "MDV", "Maldives" },
        { "ML", "MLI", "Mali" },
        { "MT", "MLT", "Malta" },
        { "MH", "MHL", "Marshall Islands" },
        { "MQ", "MTQ", "Martinique" },
        { "MR", "MRT", "Mauritania" },
        { "MU", "MUS", "Mauritius" },
        { "YT", "MYT", "Mayotte" },
        { "MX", "MEX", "Mexico" },
        { "FM", "FSM", "Micronesia" },
        { "MD", "MDA", "Moldova" },
        { "MC", "MCO", "Monaco" },
        { "MN", "MNG", "Mongolia" },
        { "ME", "MNE", "Montenegro" },
        { "MS", "MSR", "Montserrat" },
        { "MA", "MAR", "Morocco" },
        { "MZ", "MOZ", "Mozambique" },
        { "MM", "MMR", "Myanmar" },
        { "NA", "NAM", "Namibia" },
        { "NR", "NRU", "Nauru" },
        { "NP", "NPL", "Nepal" },
        { "NL", "NLD", "Netherlands" },
        { "NC", "NCL", "New Caledonia" },
        { "NZ", "NZL", "New Zealand" },
        { "NI", "NIC", "Nicaragua" },
        { "NE", "NER", "Niger" },
        { "NG", "NGA", "Nigeria" },
        { "NU", "NIU", "Niue" },
        { "NF", "NFK", "Norfolk Island" },
        { "KP", "PRK", "North Korea" },
        { "MK", "MKD", "North Macedonia" },
        { "MP", "MNP", "Northern Mariana Islands" },
        { "NO", "NOR", "Norway" },
        { "OM", "OMN", "Oman" },
        { "PK", "PAK", "Pakistan" },
        { "PW", "PLW", "Palau" },
        { "PS", "PSE", "Palestine" },
        { "PA", "PAN", "Panama" },
        { "PG", "PNG", "Papua New Guinea" },
        { "PY", "PRY", "Paraguay" },
        { "PE", "PER", "Peru" },
        { "PH", "PHL", "Philippines" },
        { "PN", "PCN", "Pitcairn Islands" },
        { "PL", "POL", "Poland" },
        { "PT", "PRT", "Portugal" },
        { "PR", "PRI", "Puerto Rico" },
        { "QA", "QAT", "Qatar" },
        { "RE", "REU", "R\xC3\xA9union" },
        { "RO", "ROU", "Romania" },
        { "RU", "RUS", "Russia" },
        { "RW", "RWA", "Rwanda" },
        { "BL", "BLM", "Saint Barth\xC3\xA9lemy" },
        { "SH", "SHN", "Saint Helena, Ascension and Tristan da Cunha" },
        { "KN", "KNA", "Saint Kitts and Nevis" },
        { "LC", "LCA", "Saint Lucia" },
        { "MF", "MAF", "Saint Martin" },
        { "PM", "SPM", "Saint Pierre and Miquelon" },
        { "VC", "VCT", "Saint Vincent and the Grenadines" },
        { "WS", "WSM", "Samoa" },
        { "SM", "SMR", "San Marino" },
        { "ST", "STP", "Sao Tome and Principe" },
        { "SA", "SAU", "Saudi Arabia" },
        { "SN", "SEN", "Senegal" },
        { "RS", "SRB", "Serbia" },
        { "SC", "SYC", "Seychelles" },
        { "SL", "SLE", "Sierra Leone" },
        { "SG", "SGP", "Singapore" },
        { "SX", "SXM", "Sint Maarten" },
        { "SK", "SVK", "Slovakia" },
        { "SI", "SVN", "Slovenia" },
        { "SB", "SLB", "Solomon Islands" },
        { "SO", "SOM", "Somalia" },
        { "ZA", "ZAF", "South Africa" },
        { "GS", "SGS", "South Georgia and the South Sandwich Islands" },
        { "KR", "KOR", "South Korea" },
        { "SS", "SSD", "South Sudan" },
        { "ES", "ESP", "Spain" },
        { "LK", "LKA", "Sri Lanka" },
        { "SD", "SDN", "Sudan" },
        { "SR", "SUR", "Suriname" },
        { "SJ", "SJM", "Svalbard and Jan Mayen" },
        { "SE", "SWE", "Sweden" },
        { "CH", "CHE", "Switzerland" },
        { "SY", "SYR", "Syria" },
        { "TW", "TWN", "Taiwan" },
        { "TJ", "TJK", "Tajikistan" },
        { "TZ", "TZA", "Tanzania" },
        { "TH", "THA", "Thailand" },
        { "TL", "TLS", "Timor-Leste" },
        { "TG", "TGO", "Togo" },
        { "TK", "TKL", "Tokelau" },
        { "TO", "TON", "Tonga" },
        { "TT", "TTO", "Trinidad and Tobago" },
        { "TN", "TUN", "Tunisia" },
        { "TR", "TUR", "Turkey" },
        { "TM", "TKM", "Turkmenistan" },
        { "TC", "TCA", "Turks and Caicos Islands" },
        { "TV", "TUV", "Tuvalu" },
        { "UG", "UGA", "Uganda" },
        { "UA", "UKR", "Ukraine" },
        { "AE", "ARE", "United Arab Emirates" },
        { "GB", "GBR", "United Kingdom" },
        { "US", "USA", "United States" },
        { "UM", "UMI", "United States Minor Outlying Islands" },
        { "VI", "VIR", "US Virgin Islands" },
        { "UY", "URY", "Uruguay" },
        { "UZ", "UZB", "Uzbekistan" },
        { "VU", "VUT", "Vanuatu" },
        { "VE", "VEN", "Venezuela" },
        { "VN", "VNM", "Vietnam" },
        { "WF", "WLF", "Wallis and Futuna" },
        { "EH", "ESH", "Western Sahara" },
        { "YE", "YEM", "Yemen" },
        { "ZM", "ZMB", "Zambia" },
        { "ZW", "ZWE", "Zimbabwe" },
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
            // Adds or replaces one country. Names and ISO3 codes of a replaced entry stay accepted as
            // aliases, so an override file can rename a country without breaking old values.
            auto put = [&](const std::string& a2, const std::string& a3, const std::string& nm) {
                d.a2name[a2] = nm;
                d.a3to2[a3] = a2;
                d.a2to3[a2] = a3;
                d.name2to2[lower(nm)] = a2;
                };
            for (const CountryRow& r : kBuiltinCountries) put(r.a2, r.a3, r.name);

            // Optional override / extension file next to the DLL.
            std::ifstream f(componentDir() / "countries.txt");
            std::string line;
            while (std::getline(f, line)) {
                if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line.erase(0, 3);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                auto p1 = line.find('|');
                auto p2 = p1 == std::string::npos ? p1 : line.find('|', p1 + 1);
                if (p2 == std::string::npos) continue;
                const std::string a2 = upper(line.substr(0, p1));
                const std::string a3 = upper(line.substr(p1 + 1, p2 - p1 - 1));
                const std::string nm = line.substr(p2 + 1);
                if (a2.empty() || a3.empty() || nm.empty()) continue;
                put(a2, a3, nm);
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

    // Where an artist table is looked for when its path box is empty.
    enum DefaultLocation {
        kLocComponent = 0,          // next to the DLL only
        kLocJsData = 1,             // profile\js_data\<file>, else next to the DLL
        kLocJSplitterPackage = 2    // profile\foo_uie_jsplitter\package_data\{GUID}\<file> if it exists, else next to the DLL
    };

    fs::path defaultTablePath(const char* fileName, int location) {
        const fs::path inComponent = componentDir() / fileName;
        if (location == kLocComponent) return inComponent;
        const std::wstring& prof = profileDirWide();
        if (prof.empty()) return inComponent;
        std::error_code ec;
        if (location == kLocJSplitterPackage) {
            const fs::path inPackage = fs::path(prof) / L"foo_uie_jsplitter" / L"package_data" /
                L"{BA9557CE-7B4B-4E0E-9373-99F511E81252}" / fileName;
            if (fs::exists(inPackage, ec)) return inPackage;
            return inComponent;
        }
        const fs::path inProfile = fs::path(prof) / L"js_data" / fileName;
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
        const char* defaultFile, int location, bool arrayFormat) {
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
        const fs::path p = custom.empty() ? defaultTablePath(defaultFile, location) : expandPath(custom);
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
        return loadArtistTable(t, trim(g_cfgArtistFile.get()), "country_codes.json", kLocJSplitterPackage, false);
    }

    std::shared_ptr<const ArtistMap> artistMap2() {
        static ArtistTable t;
        return loadArtistTable(t, trim(g_cfgArtistFile2.get()), "worldMap.json", kLocJsData, true);
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
            {
                std::lock_guard<std::mutex> lock(d.m);
                d.track = p_track;
                d.artists.clear();
            }
            ++g_generation;
        }
        void on_playback_stop(play_control::t_stop_reason) override {
            DynamicArtists& d = dynamicArtists();
            {
                std::lock_guard<std::mutex> lock(d.m);
                d.track.release();
                d.artists.clear();
            }
            ++g_generation;
        }
        void on_playback_dynamic_info_track(const file_info& p_info) override {
            std::vector<std::string> artists;
            const t_size total = p_info.meta_get_count();
            for (t_size i = 0; i < total; ++i) {
                const t_size values = p_info.meta_enum_value_count(i);
                if (values == 0) continue;
                const std::string name = lower(p_info.meta_enum_name(i));
                if (name == "artist")
                    for (t_size k = 0; k < values; ++k) artists.emplace_back(p_info.meta_enum_value(i, k));
            }
            DynamicArtists& d = dynamicArtists();
            {
                std::lock_guard<std::mutex> lock(d.m);
                d.artists = std::move(artists);
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

    // Scripts may be written over several lines: whole-line // comments are dropped, leading and
    // trailing whitespace of every line is removed and the lines are joined into the one-liner
    // that is compiled.
    std::string flattenScript(const std::string& text) {
        std::string out;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(pos, end - pos);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
            size_t first = 0;
            while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) ++first;
            line.erase(0, first);
            if (!line.empty() && line.compare(0, 2, "//") != 0) out += line;
            if (end == text.size()) break;
            pos = end + 1;
        }
        return out;
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
        const std::string flat = flattenScript(src ? src : "");

        ScriptCache& c = sourceScriptCache(slot);
        std::lock_guard<std::mutex> lock(c.m);
        if (!c.init || c.raw != flat) {
            titleformat_object::ptr obj;
            titleformat_compiler::get()->compile_safe(obj, flat.c_str());
            c.obj = obj;
            c.raw = flat;
            c.init = true;
        }
        return c.obj;
    }

    // What one source produced for one track: the resolved countries plus the raw script results
    // (published as the _argument / _fallbackargument fields for debugging).
    struct SourceResult {
        std::vector<std::string> countries;
        std::string argument;           // raw (trimmed) result of the source script
        std::string fallbackArgument;   // raw result of the fallback script; empty when it did not run
    };

    struct CountryCacheEntry {
        metadb_info_container::ptr info;
        unsigned generation = 0;
        std::chrono::steady_clock::time_point stored;
        SourceResult result;
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
        }
    };
    initquit_factory_t<quit_cleanup> g_quit_cleanup;

    std::string evalCompiled(metadb_handle* h, const titleformat_object::ptr& script) {
        if (script.is_empty()) return {};
        pfc::string8 result;
        h->format_title(nullptr, result, script, nullptr);
        return trim(result.c_str());
    }

    std::string evalScript(metadb_handle* h, size_t slot) {
        return evalCompiled(h, compiledScript(slot));
    }

    std::vector<std::string> parseCountryList(const std::string& whole) {
        std::vector<std::string> countries;
        if (whole.empty()) return countries;
        size_t pos = 0;
        while (pos <= whole.size()) {
            size_t end = whole.find_first_of(",;/+", pos);
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

    SourceResult resolveSource(metadb_handle* h, size_t sourceIndex) {
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
                return it->second.result;
        }

        SourceResult result;
        result.argument = evalScript(h, sourceIndex);
        result.countries = parseCountryList(result.argument);
        if (result.countries.empty()) {
            result.fallbackArgument = evalScript(h, 5 + sourceIndex);
            if (!result.fallbackArgument.empty()) result.countries = lookupArtistName(result.fallbackArgument);
        }

        if (info.is_valid()) {
            CountryCache& cc = countryCache();
            std::lock_guard<std::mutex> lock(cc.m);
            if (cc.map.size() > 4096) cc.map.clear();
            CountryCacheEntry& e = cc.map[key];
            e.info = info;
            e.generation = generation;
            e.stored = clock::now();
            e.result = result;
        }
        return result;
    }

    std::vector<std::string> countriesOf(metadb_handle* h, size_t sourceIndex) {
        return resolveSource(h, sourceIndex).countries;
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
    constexpr int ID_FIELDLIST = 1074;
    constexpr int ID_COPYFIELD = 1075;
    constexpr int ID_IMPORT = 1072;
    constexpr int ID_EXPORT = 1073;
    constexpr UINT_PTR kPreviewTimer = 77;       // repeating: keeps the preview in step with the selected track
    constexpr UINT_PTR kPreviewSoon = 78;        // one-shot: shortly after a script was edited
    constexpr int ID_EDITSCRIPT_BASE = 1060;   // 1060..1069: the "..." buttons (slot 0-4 sources, 5-9 fallbacks)
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


    // Field kinds published per source (the order is the field index order of the provider).
    constexpr size_t kFieldKindCount = 9;
    const char* const kFieldKindNames[kFieldKindCount] = {
        "emoji", "path", "name", "iso2", "iso3", "region", "subregion", "argument", "fallbackargument" };
    const char* const kFieldKindInfo[kFieldKindCount] = {
        "Flag emoji (several countries are separated by a space)",
        "Full path of the flag image, only if the file exists",
        "English country name",
        "ISO 3166-1 alpha-2 code, e.g. GR",
        "ISO 3166-1 alpha-3 code, e.g. GRC",
        "Cultural region, e.g. Europe",
        "Cultural subregion, e.g. Southern Europe",
        "Debug: raw result of the source's titleformat script",
        "Debug: raw result of the fallback script (empty when it did not run)" };

    // ---- Shared UI font for the component's own dialogs: the system message font (as used by the
    // preferences tree) at the DPI of the window it is shown on ----
    UINT uiDpiFor(HWND wnd) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        static const auto fn = []() -> GetDpiForWindowFn {
            const HMODULE user32 = GetModuleHandleW(L"user32.dll");
            return user32 ? reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
            }();
        if (fn && wnd) {
            const UINT dpi = fn(wnd);
            if (dpi != 0) return dpi;
        }
        HDC dc = GetDC(wnd);
        const int logpix = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(wnd, dc);
        return logpix > 0 ? static_cast<UINT>(logpix) : 96;
    }

    HFONT createUiFont(UINT dpi) {
        LOGFONTW lf{};
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        using SpiForDpiFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
        static const auto spiForDpi = []() -> SpiForDpiFn {
            const HMODULE user32 = GetModuleHandleW(L"user32.dll");
            return user32 ? reinterpret_cast<SpiForDpiFn>(GetProcAddress(user32, "SystemParametersInfoForDpi")) : nullptr;
            }();
        if (spiForDpi && spiForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi)) lf = ncm.lfMessageFont;
        else if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) lf = ncm.lfMessageFont;
        else GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
        return CreateFontIndirectW(&lf);
    }

    // ---- Themed message box (MessageBox does not follow foobar2000's dark mode) ----
    // Returns IDYES / IDNO for a Yes/No box and IDOK for an OK box.
    class themed_message {
    public:
        static int show(HWND owner, const char* title, const std::string& text, bool yesNo, LPCWSTR icon) {
            themed_message box(owner, title, text, yesNo, icon);
            return box.run();
        }

    private:
        themed_message(HWND owner, const char* title, const std::string& text, bool yesNo, LPCWSTR icon) :
            m_owner(owner), m_yesNo(yesNo), m_icon(icon),
            m_title(pfc::stringcvt::string_wide_from_utf8(title).get_ptr()),
            m_text(pfc::stringcvt::string_wide_from_utf8(text.c_str()).get_ptr()),
            m_brDark(CreateSolidBrush(RGB(32, 32, 32))) {
        }
        ~themed_message() { if (m_brDark) DeleteObject(m_brDark); if (m_font) DeleteObject(m_font); }
        themed_message(const themed_message&) = delete;
        themed_message& operator=(const themed_message&) = delete;

        static int dpiOf(HWND wnd) { return static_cast<int>(uiDpiFor(wnd)); }

        HWND addChild(LPCWSTR cls, LPCWSTR text, DWORD style, int x, int y, int w, int h, int id) {
            HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, m_wnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), core_api::get_my_instance(), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(m_font ? m_font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))), TRUE);
            return c;
        }

        int run() {
            static const bool registered = [] {
                WNDCLASSW wc{};
                wc.lpfnWndProc = proc;
                wc.hInstance = core_api::get_my_instance();
                wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                wc.lpszClassName = L"foo_countryflags_message";
                return RegisterClassW(&wc) != 0;
                }();
            (void)registered;

            const int dpi = dpiOf(m_owner ? m_owner : GetDesktopWindow());
            auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
            const int margin = S(14), gap = S(12), textW = S(360), btnW = S(84), btnH = S(26), iconSz = S(32);
            m_font = createUiFont(static_cast<UINT>(dpi));

            int textH = 0;
            {
                HDC dc = GetDC(nullptr);
                HGDIOBJ old = SelectObject(dc, m_font ? static_cast<HGDIOBJ>(m_font) : GetStockObject(DEFAULT_GUI_FONT));
                RECT rc{ 0, 0, textW, 0 };
                DrawTextW(dc, m_text.c_str(), -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                textH = rc.bottom;
                SelectObject(dc, old);
                ReleaseDC(nullptr, dc);
            }
            const int contentH = (std::max)(textH, iconSz);
            const int clientW = margin + iconSz + gap + textW + margin;
            const int clientH = margin + contentH + gap + btnH + margin;

            const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
            const DWORD exStyle = WS_EX_DLGMODALFRAME;
            RECT wr{ 0, 0, clientW, clientH };
            AdjustWindowRectEx(&wr, style, FALSE, exStyle);
            const int w = wr.right - wr.left, h = wr.bottom - wr.top;

            RECT area{};
            if (!m_owner || !GetWindowRect(m_owner, &area)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
            const int x = area.left + ((area.right - area.left) - w) / 2;
            const int y = area.top + ((area.bottom - area.top) - h) / 2;

            m_wnd = CreateWindowExW(exStyle, L"foo_countryflags_message", m_title.c_str(), style, x, y, w, h,
                m_owner, nullptr, core_api::get_my_instance(), this);
            if (!m_wnd) return m_yesNo ? IDNO : IDOK;

            HWND icon = addChild(L"STATIC", nullptr, SS_ICON, margin, margin, iconSz, iconSz, 0);
            SendMessageW(icon, STM_SETICON, reinterpret_cast<WPARAM>(LoadImageW(nullptr, m_icon, IMAGE_ICON, iconSz, iconSz, LR_SHARED)), 0);
            addChild(L"STATIC", m_text.c_str(), SS_LEFT | SS_NOPREFIX, margin + iconSz + gap,
                margin + (contentH - textH) / 2, textW, textH, 0);

            const int btnY = clientH - margin - btnH;
            const int lastX = clientW - margin - btnW;
            HWND defButton = nullptr;
            if (m_yesNo) {
                defButton = addChild(L"BUTTON", L"Yes", WS_TABSTOP | BS_DEFPUSHBUTTON, lastX - S(8) - btnW, btnY, btnW, btnH, IDYES);
                addChild(L"BUTTON", L"No", WS_TABSTOP | BS_PUSHBUTTON, lastX, btnY, btnW, btnH, IDNO);
            }
            else {
                defButton = addChild(L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, lastX, btnY, btnW, btnH, IDOK);
            }
            m_dark.AddDialogWithControls(m_wnd);

            if (m_owner) EnableWindow(m_owner, FALSE);
            ShowWindow(m_wnd, SW_SHOW);
            SetForegroundWindow(m_wnd);
            SetFocus(defButton);

            MSG msg;
            while (!m_done) {
                const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
                if (got <= 0) {
                    if (got == 0) PostQuitMessage((int)msg.wParam);
                    break;
                }
                if (!IsDialogMessageW(m_wnd, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }

            if (m_owner) EnableWindow(m_owner, TRUE);
            DestroyWindow(m_wnd);
            m_wnd = nullptr;
            if (m_owner) SetForegroundWindow(m_owner);
            return m_result ? m_result : (m_yesNo ? IDNO : IDOK);
        }

        static LRESULT CALLBACK proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
            auto* self = reinterpret_cast<themed_message*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                self = static_cast<themed_message*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
                SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            return self ? self->handle(wnd, msg, wp, lp) : DefWindowProcW(wnd, msg, wp, lp);
        }

        LRESULT handle(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
            const bool dark = m_dark;
            switch (msg) {
            case WM_ERASEBKGND: {
                RECT rc{};
                GetClientRect(wnd, &rc);
                FillRect(reinterpret_cast<HDC>(wp), &rc, dark ? m_brDark : GetSysColorBrush(COLOR_BTNFACE));
                return 1;
            }
            case WM_CTLCOLORSTATIC: {
                HDC dc = reinterpret_cast<HDC>(wp);
                if (dark) SetTextColor(dc, RGB(240, 240, 240));
                SetBkMode(dc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(dark ? m_brDark : GetSysColorBrush(COLOR_BTNFACE));
            }
            case WM_COMMAND: {
                int id = LOWORD(wp);
                if (id == IDCANCEL) id = m_yesNo ? IDNO : IDOK;
                else if (id == IDOK && m_yesNo) id = IDYES;
                if (id == IDYES || id == IDNO || id == IDOK) {
                    m_result = id;
                    m_done = true;
                    return 0;
                }
                break;
            }
            case WM_CLOSE:
                m_result = m_yesNo ? IDNO : IDOK;
                m_done = true;
                return 0;
            }
            return DefWindowProcW(wnd, msg, wp, lp);
        }

        HWND m_owner;
        bool m_yesNo;
        LPCWSTR m_icon;
        std::wstring m_title, m_text;
        HBRUSH m_brDark;
        HFONT m_font = nullptr;
        HWND m_wnd = nullptr;
        int m_result = 0;
        bool m_done = false;
        fb2k::CCoreDarkModeHooks m_dark;
    };

    // ---- Preview helpers (used by the preferences page and the script editor) ----

    // The track a preview is evaluated for: a selected track always wins, even while another track
    // is playing (active playlist selection, then any other selection, then the focused playlist
    // row); the playing track is used when none of these exist.
    metadb_handle_ptr pickPreviewTrack() {
        metadb_handle_ptr playing;
        playback_control::get()->get_now_playing(playing);
        metadb_handle_list sel;
        playlist_manager::get()->activeplaylist_get_selected_items(sel);
        if (sel.get_count() == 0) ui_selection_manager::get()->get_selection(sel);
        if (sel.get_count() > 0) return sel[0];
        metadb_handle_ptr focused;
        if (playlist_manager::get()->activeplaylist_get_focus_item_handle(focused) && focused.is_valid()) return focused;
        return playing;
    }

    std::string describeCountries(const std::vector<std::string>& isos) {
        std::string out;
        for (const auto& iso : isos) {
            if (!out.empty()) out += ", ";
            out += iso;
            const auto it = db().a2name.find(iso);
            if (it != db().a2name.end()) out += " (" + it->second + ")";
        }
        return out;
    }

    std::string trackTitle(metadb_handle* track) {
        titleformat_object::ptr nameScript;
        titleformat_compiler::get()->compile_safe(nameScript, "%artist% - %title%");
        return evalCompiled(track, nameScript);
    }

    // Evaluates script text (flattened first); returns false when it does not compile.
    bool evalScriptText(metadb_handle* track, const std::string& text, std::string& value) {
        value.clear();
        const std::string flat = flattenScript(text);
        if (flat.empty()) return true;
        titleformat_object::ptr obj;
        if (!titleformat_compiler::get()->compile(obj, flat.c_str())) return false;
        value = evalCompiled(track, obj);
        return true;
    }

    // One preview line for a source: what the script returned and which countries that gives,
    // including the fallback when the script alone finds none.
    std::string previewSourceLine(metadb_handle* track, int number, const std::string& script, const std::string& fallback) {
        std::string line = "Source " + std::to_string(number) + ": ";
        if (flattenScript(script).empty() && flattenScript(fallback).empty()) return line + "(no script)";
        std::string value;
        if (!evalScriptText(track, script, value)) return line + "(the script does not compile)";
        line += value.empty() ? std::string("(empty)") : value;
        const std::vector<std::string> countries = parseCountryList(value);
        if (!countries.empty()) return line + " -> " + describeCountries(countries);
        if (flattenScript(fallback).empty()) return line + " -> (no country)";
        std::string fvalue;
        if (!evalScriptText(track, fallback, fvalue)) return line + " | fallback does not compile";
        line += " | fallback: " + (fvalue.empty() ? std::string("(empty)") : fvalue);
        const std::vector<std::string> found = fvalue.empty() ? std::vector<std::string>() : lookupArtistName(fvalue);
        return line + (found.empty() ? std::string(" -> (not found)") : " -> " + describeCountries(found));
    }

    // ---- Large script editor (modal, resizable) with live preview ----
    constexpr int ID_SE_EDIT = 3001;
    constexpr int ID_SE_HINT = 3002;
    constexpr int ID_SE_PLABEL = 3003;
    constexpr int ID_SE_RESULT = 3004;
    constexpr UINT_PTR kSePreviewTimer = 1;

    struct ScriptEditorState {
        std::string text;
        bool accepted = false;
        int kind = 0;               // 0 = titleformat script (country), 1 = fallback script (artist name)
        HFONT font = nullptr;       // monospace font of the script box
        HFONT uiFont = nullptr;     // font of the labels, preview and buttons
        fb2k::CCoreDarkModeHooks dark;
    };

    // Evaluates the script text from the editor (saved or not) and describes what the component
    // would make of it.
    std::string scriptTestReport(const std::string& text, int kind) {
        const metadb_handle_ptr track = pickPreviewTrack();
        if (track.is_empty()) return "Select a track in the playlist (or start playback) to see a preview.";

        const std::string flat = flattenScript(text);
        if (flat.empty()) return "(empty script)";
        titleformat_object::ptr obj;
        if (!titleformat_compiler::get()->compile(obj, flat.c_str())) return "The script does not compile.";

        std::string report = "Track: " + trackTitle(track.get_ptr());
        const std::string value = evalCompiled(track.get_ptr(), obj);
        report += "\r\nResult: " + (value.empty() ? std::string("(empty)") : value);
        if (kind == 0) {
            const std::string countries = describeCountries(parseCountryList(value));
            report += "\r\nCountries: " + (countries.empty() ? std::string("(none recognised - the fallback script would run)") : countries);
        }
        else {
            const std::string countries = value.empty() ? std::string() : describeCountries(lookupArtistName(value));
            report += "\r\nArtist tables: " + (countries.empty() ? std::string("(artist not found)") : countries);
        }
        return report;
    }

    LRESULT CALLBACK scriptEditSubclass(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
        // Ctrl+A selects everything (multi-line EDIT controls do not do this themselves)
        if (msg == WM_KEYDOWN && wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SendMessageW(wnd, EM_SETSEL, 0, -1);
            return 0;
        }
        if (msg == WM_CHAR && wp == 1) return 0;
        if (msg == WM_NCDESTROY) RemoveWindowSubclass(wnd, scriptEditSubclass, id);
        return DefSubclassProc(wnd, msg, wp, lp);
    }

    int editorDpi(HWND wnd) {
        HDC dc = GetDC(wnd);
        const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(wnd, dc);
        return dpi > 0 ? dpi : 96;
    }

    std::string editControlText(HWND edit) {
        const int len = GetWindowTextLengthW(edit);
        std::wstring wide(static_cast<size_t>(len) + 1, L'\0');
        GetWindowTextW(edit, wide.data(), len + 1);
        wide.resize(len);
        pfc::stringcvt::string_utf8_from_wide utf8(wide.c_str());
        std::string text;
        for (const char* c = utf8.get_ptr(); *c; ++c) if (*c != '\r') text += *c;
        return text;
    }

    void scriptEditorUpdatePreview(HWND dlg, const ScriptEditorState* st) {
        if (!st) return;
        const std::string report = scriptTestReport(editControlText(GetDlgItem(dlg, ID_SE_EDIT)), st->kind);
        pfc::stringcvt::string_wide_from_utf8 out(report.c_str());
        SetWindowTextW(GetDlgItem(dlg, ID_SE_RESULT), out.get_ptr());
    }

    void scriptEditorLayout(HWND dlg) {
        RECT rc{};
        GetClientRect(dlg, &rc);
        const int dpi = editorDpi(dlg);
        auto S = [&](int v) { return MulDiv(v, dpi, 96); };
        const int margin = S(10), btnW = S(80), btnH = S(26), hintH = S(34), labelH = S(18), resultH = S(84);
        const int w = rc.right, h = rc.bottom;
        const int buttonsY = h - margin - btnH;
        const int resultY = buttonsY - margin - resultH;
        const int labelY = resultY - S(2) - labelH;
        const int hintY = labelY - S(4) - hintH;
        SetWindowPos(GetDlgItem(dlg, ID_SE_EDIT), nullptr, margin, margin, w - 2 * margin,
            (std::max)(S(40), hintY - S(6) - margin), SWP_NOZORDER);
        SetWindowPos(GetDlgItem(dlg, ID_SE_HINT), nullptr, margin, hintY, w - 2 * margin, hintH, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(dlg, ID_SE_PLABEL), nullptr, margin, labelY, w - 2 * margin, labelH, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(dlg, ID_SE_RESULT), nullptr, margin, resultY, w - 2 * margin, resultH, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(dlg, IDOK), nullptr, w - 2 * margin - 2 * btnW, buttonsY, btnW, btnH, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(dlg, IDCANCEL), nullptr, w - margin - btnW, buttonsY, btnW, btnH, SWP_NOZORDER);
    }

    INT_PTR CALLBACK scriptEditorProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
        ScriptEditorState* st = reinterpret_cast<ScriptEditorState*>(GetWindowLongPtrW(dlg, DWLP_USER));
        switch (msg) {
        case WM_INITDIALOG: {
            st = reinterpret_cast<ScriptEditorState*>(lp);
            SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(st));
            HINSTANCE inst = core_api::get_my_instance();
            const int dpi = editorDpi(dlg);
            st->uiFont = createUiFont(static_cast<UINT>(dpi));
            HGDIOBJ gui = st->uiFont ? static_cast<HGDIOBJ>(st->uiFont) : GetStockObject(DEFAULT_GUI_FONT);

            st->font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

            HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
                0, 0, 10, 10, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SE_EDIT)), inst, nullptr);
            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(st->font ? static_cast<HGDIOBJ>(st->font) : gui), TRUE);
            SetWindowSubclass(edit, scriptEditSubclass, 1, 0);

            std::string crlf;
            for (char c : st->text) {
                if (c == '\r') continue;
                if (c == '\n') crlf += "\r\n"; else crlf += c;
            }
            pfc::stringcvt::string_wide_from_utf8 wide(crlf.c_str());
            SetWindowTextW(edit, wide.get_ptr());

            HWND hint = CreateWindowExW(0, L"STATIC",
                L"Line breaks and indentation are removed, and lines that start with // are ignored, when the script is compiled. "
                L"Do not rely on line breaks inside quoted text.",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, dlg,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SE_HINT)), inst, nullptr);
            SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(gui), TRUE);
            HWND plabel = CreateWindowExW(0, L"STATIC", L"Preview:", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                0, 0, 10, 10, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SE_PLABEL)), inst, nullptr);
            SendMessageW(plabel, WM_SETFONT, reinterpret_cast<WPARAM>(gui), TRUE);
            HWND result = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                0, 0, 10, 10, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SE_RESULT)), inst, nullptr);
            SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(gui), TRUE);
            HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0, 0, 10, 10, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), inst, nullptr);
            SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(gui), TRUE);
            HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), inst, nullptr);
            SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(gui), TRUE);

            scriptEditorLayout(dlg);
            st->dark.AddDialogWithControls(dlg);
            scriptEditorUpdatePreview(dlg, st);
            SetFocus(edit);
            SendMessageW(edit, EM_SETSEL, 0, 0);
            return FALSE;   // focus was set explicitly
        }
        case WM_SIZE:
            scriptEditorLayout(dlg);
            return TRUE;
        case WM_GETMINMAXINFO: {
            const int dpi = editorDpi(dlg);
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = MulDiv(360, dpi, 96);
            mm->ptMinTrackSize.y = MulDiv(420, dpi, 96);
            return TRUE;
        }
        case WM_TIMER:
            if (wp == kSePreviewTimer) {
                KillTimer(dlg, kSePreviewTimer);
                scriptEditorUpdatePreview(dlg, st);
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wp) == ID_SE_EDIT && HIWORD(wp) == EN_CHANGE) {
                SetTimer(dlg, kSePreviewTimer, 300, nullptr);   // evaluate shortly after typing stops
                return TRUE;
            }
            if (LOWORD(wp) == IDOK && st) {
                st->text = editControlText(GetDlgItem(dlg, ID_SE_EDIT));
                st->accepted = true;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
            break;
        case WM_DESTROY:
            KillTimer(dlg, kSePreviewTimer);
            if (st && st->font) { DeleteObject(st->font); st->font = nullptr; }
            if (st && st->uiFont) { DeleteObject(st->uiFont); st->uiFont = nullptr; }
            break;
        }
        return FALSE;
    }

    // Shows the editor; returns true (and updates text) when the user pressed OK.
    bool editScriptDialog(HWND owner, const wchar_t* title, std::string& text, int kind) {
        std::vector<WORD> tpl;
        auto push32 = [&](DWORD v) { tpl.push_back(LOWORD(v)); tpl.push_back(HIWORD(v)); };
        push32(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX);
        push32(0);
        tpl.push_back(0);                                   // no template items
        tpl.push_back(0); tpl.push_back(0);                 // x, y
        tpl.push_back(293); tpl.push_back(250);             // cx, cy in dialog units
        tpl.push_back(0); tpl.push_back(0);                 // no menu, default class
        for (const wchar_t* c = title; *c; ++c) tpl.push_back(static_cast<WORD>(*c));
        tpl.push_back(0);
        tpl.push_back(9);                                   // font size
        for (const wchar_t* c = L"Segoe UI"; *c; ++c) tpl.push_back(static_cast<WORD>(*c));
        tpl.push_back(0);

        ScriptEditorState state;
        state.text = text;
        state.kind = kind;
        DialogBoxIndirectParamW(core_api::get_my_instance(), reinterpret_cast<LPCDLGTEMPLATEW>(tpl.data()),
            owner, scriptEditorProc, reinterpret_cast<LPARAM>(&state));
        if (!state.accepted) return false;
        text = state.text;
        return true;
    }

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT 0x0050
#endif

    class country_flags_preferences_instance : public preferences_page_instance {
    public:
        country_flags_preferences_instance(HWND parent, preferences_page_callback::ptr callback)
            : m_callback(callback), m_wnd(nullptr), m_tabWnd(nullptr), m_tab1Wnd(nullptr), m_tab2Wnd(nullptr), m_tab3Wnd(nullptr), m_fieldList(nullptr), m_fieldHint(nullptr), m_copyBtn(nullptr),
            m_ready(false), m_originalOrder(0),
            m_note(nullptr), m_boldFont(nullptr), m_settingScript(false),
            m_previewLabel(nullptr), m_previewBox(nullptr), m_importBtn(nullptr), m_exportBtn(nullptr),
            m_flagLabel(nullptr), m_artistLabel(nullptr), m_artist2Label(nullptr),
            m_orderLabel(nullptr), m_orderWorld(nullptr), m_orderFlat(nullptr),
            m_statusFlag(nullptr), m_statusArtist(nullptr), m_statusArtist2(nullptr),
            m_statusFont(nullptr), m_brDark(CreateSolidBrush(RGB(32, 32, 32))) {

            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES };
            InitCommonControlsEx(&icc);

            std::memset(m_rowLabel, 0, sizeof(m_rowLabel));
            std::memset(m_caption, 0, sizeof(m_caption));
            std::memset(m_editBtn, 0, sizeof(m_editBtn));
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
                m_parent = parent;
                buildFonts();
                m_tabWnd = CreateWindowExW(0, WC_TABCONTROLW, L"",
                    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                    0, 0, initW, initH, m_wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TAB)),
                    hInstance, nullptr);
                SendMessageW(m_tabWnd, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);

                SetWindowTheme(m_tabWnd, isDarkMode() ? L"DarkMode_Explorer" : L"", nullptr);
                SetWindowSubclass(m_tabWnd, TabControlSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

                TCITEMW tie{};
                tie.mask = TCIF_TEXT;
                tie.pszText = const_cast<LPWSTR>(L"Sources");
                TabCtrl_InsertItem(m_tabWnd, 0, &tie);
                tie.pszText = const_cast<LPWSTR>(L"Folders");
                TabCtrl_InsertItem(m_tabWnd, 1, &tie);
                tie.pszText = const_cast<LPWSTR>(L"Fields");
                TabCtrl_InsertItem(m_tabWnd, 2, &tie);

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
                m_tab3Wnd = CreateWindowExW(0, L"CountryFlagsTabChildPage", L"",
                    WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                    rcTab.left, rcTab.top, tabW, tabH, m_tabWnd, nullptr, hInstance, this);
                ShowWindow(m_tab3Wnd, SW_HIDE);
                createTab1Controls();
                createTab2Controls();
                createTab3Controls();

                loadValues(false);
                updateStatus();
                refreshFieldList();

                m_darkMode.AddDialogWithControls(m_wnd);
                m_tab1DarkMode.AddDialogWithControls(m_tab1Wnd);
                m_tab2DarkMode.AddDialogWithControls(m_tab2Wnd);
                m_tab3DarkMode.AddDialogWithControls(m_tab3Wnd);
                m_ready = true;

                layoutControls(initW, initH);
                updatePreview();
                SetTimer(m_wnd, kPreviewTimer, 1500, nullptr);
            }
        }

        bool isDarkMode() const {
            // Query dark mode status directly from the foobar2000 dark mode helper object
            return m_darkMode;
        }

        ~country_flags_preferences_instance() {
            if (m_statusFont) DeleteObject(m_statusFont);
            if (m_boldFont) DeleteObject(m_boldFont);
            if (m_uiFont) DeleteObject(m_uiFont);
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
            std::string fieldsBefore[5];
            for (size_t i = 0; i < 5; ++i) fieldsBefore[i] = fieldPrefix(i);
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

            // Field names are only read at startup, so a changed suffix needs a restart.
            // The settings are written on exit, so the new suffixes survive it.
            bool renamed = false;
            for (size_t i = 0; i < 5; ++i)
                if (fieldPrefix(i) != fieldsBefore[i]) { renamed = true; break; }
            if (renamed) {
                HWND owner = m_wnd ? GetAncestor(m_wnd, GA_ROOT) : nullptr;
                const int answer = themed_message::show(owner, "Country Flags",
                    "A field suffix was changed. foobar2000 reads field names at startup, so the new names only take effect after a restart.\n\nRestart foobar2000 now?",
                    true, IDI_QUESTION);
                if (answer == IDYES) {
                    fb2k::inMainThread([] {
                        standard_commands::run_main(standard_commands::guid_main_restart);
                        });
                }
            }
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
            if ((wnd == m_tab1Wnd || wnd == m_tab2Wnd || wnd == m_tab3Wnd)) {
                if (msg == WM_SIZE && wnd == m_tab3Wnd) layoutFieldsTab();
                if ((msg == WM_COMMAND || msg == WM_NOTIFY) && m_wnd) {
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
                    ShowWindow(m_tab3Wnd, sel == 2 ? SW_SHOW : SW_HIDE);
                    if (sel == 2) { ensureFieldList(); layoutFieldsTab(); refreshFieldList(); }
                    return 0;
                }
                if (nm && nm->idFrom == ID_FIELDLIST) {
                    if (nm->code == NM_DBLCLK) { copySelectedFields(); return 0; }
                    if (nm->code == LVN_ITEMCHANGED) {
                        EnableWindow(m_copyBtn, ListView_GetSelectedCount(m_fieldList) > 0);
                        return 0;
                    }
                    if (nm->code == LVN_KEYDOWN) {
                        const auto* kd = reinterpret_cast<const NMLVKEYDOWN*>(lp);
                        if (GetKeyState(VK_CONTROL) & 0x8000) {
                            if (kd->wVKey == 'C' || kd->wVKey == VK_INSERT) copySelectedFields();
                            else if (kd->wVKey == 'A') ListView_SetItemState(m_fieldList, -1, LVIS_SELECTED, LVIS_SELECTED);
                        }
                        return 0;
                    }
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
                case ID_COPYFIELD: copySelectedFields(); return 0;
                case ID_IMPORT: importSettings(); return 0;
                case ID_EXPORT: exportSettings(); return 0;
                default:
                    if (LOWORD(wp) >= ID_EDITSCRIPT_BASE && LOWORD(wp) < ID_EDITSCRIPT_BASE + 10) {
                        openScriptEditor(LOWORD(wp) - ID_EDITSCRIPT_BASE);
                        return 0;
                    }
                    break;
                }
            }
            if (msg == WM_COMMAND && HIWORD(wp) == EN_CHANGE) {
                const int scriptIdx = scriptIndex(LOWORD(wp));
                if (scriptIdx >= 0) {
                    if (m_settingScript) return 0;                // programmatic update of the displayed text
                    m_full[scriptIdx] = rawEditText(LOWORD(wp));  // user typed in the box: it replaces the stored text
                    if (m_ready) SetTimer(m_wnd, kPreviewSoon, 300, nullptr);
                }
                if (m_ready) {
                    const int id = LOWORD(wp);
                    if (id >= ID_PREFIX2 && id <= ID_PREFIX5) refreshFieldList();
                    if (id == ID_FLAG_DIR || id == ID_ARTIST_FILE || id == ID_ARTIST2_FILE) updateStatus();
                    if (m_callback.is_valid()) m_callback->on_state_changed();
                }
            }
            if (msg == WM_SIZE) layoutControls(LOWORD(lp), HIWORD(lp));
            if (msg == WM_DPICHANGED || msg == WM_DPICHANGED_AFTERPARENT) {
                layoutControls(GetClientRectWidth(m_wnd), GetClientRectHeight(m_wnd));
            }
            if (msg == WM_THEMECHANGED || msg == WM_SYSCOLORCHANGE || msg == WM_SETTINGCHANGE) {
                bool d = isDarkMode();
                if (m_tabWnd) SetWindowTheme(m_tabWnd, d ? L"DarkMode_Explorer" : L"", nullptr);
                InvalidateRect(m_wnd, nullptr, TRUE);
                if (m_tab1Wnd) InvalidateRect(m_tab1Wnd, nullptr, TRUE);
                if (m_tab2Wnd) InvalidateRect(m_tab2Wnd, nullptr, TRUE);
                if (m_tab3Wnd) InvalidateRect(m_tab3Wnd, nullptr, TRUE);
                applyFieldListColors();
            }
            if (msg == WM_TIMER && (wp == kPreviewTimer || wp == kPreviewSoon)) {
                if (wp == kPreviewSoon) KillTimer(m_wnd, kPreviewSoon);
                updatePreview();
                return 0;
            }
            if (msg == WM_DESTROY) {
                KillTimer(m_wnd, kPreviewTimer);
                KillTimer(m_wnd, kPreviewSoon);
                m_wnd = nullptr;
            }
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
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);
            return c;
        }

        HWND addEdit(HWND parent, int id) const {
            HWND c = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                core_api::get_my_instance(), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);
            return c;
        }

        HWND addButton(HWND parent, int id) const {
            HWND c = CreateWindowExW(0, L"BUTTON", L"Browse...",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                core_api::get_my_instance(), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);
            return c;
        }

        HWND addStatus(HWND parent, int id) const {
            HWND c = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
                0, 0, 10, 10, parent, id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr,
                core_api::get_my_instance(), nullptr);
            if (m_statusFont) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(m_statusFont), TRUE);
            return c;
        }

        // ---- Fonts: same face and size as the preferences tree (the host dialog's font), scaled to the window's DPI ----
        HFONT uiFont() const { return m_uiFont ? m_uiFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)); }

        LOGFONTW baseLogFont(UINT dpi) const {
            LOGFONTW lf{};
            // 1) the font of the preferences dialog that hosts this page
            if (m_parent) {
                const HFONT pf = reinterpret_cast<HFONT>(SendMessageW(m_parent, WM_GETFONT, 0, 0));
                if (pf && GetObjectW(pf, sizeof(lf), &lf) == sizeof(lf)) return lf;
            }
            // 2) the system message font at this DPI
            NONCLIENTMETRICSW ncm{};
            ncm.cbSize = sizeof(ncm);
            using SpiForDpiFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
            static const auto spiForDpi = []() -> SpiForDpiFn {
                const HMODULE user32 = GetModuleHandleW(L"user32.dll");
                return user32 ? reinterpret_cast<SpiForDpiFn>(GetProcAddress(user32, "SystemParametersInfoForDpi")) : nullptr;
                }();
            if (spiForDpi && spiForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi)) return ncm.lfMessageFont;
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) return ncm.lfMessageFont;
            GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
            return lf;
        }

        void buildFonts() {
            const UINT dpi = dpiForWindow(m_wnd);
            m_fontDpi = dpi;
            LOGFONTW lf = baseLogFont(dpi);
            m_uiFont = CreateFontIndirectW(&lf);
            lf.lfWeight = FW_BOLD;
            m_boldFont = CreateFontIndirectW(&lf);
            m_statusFont = CreateFontW(-MulDiv(12, static_cast<int>(dpi), 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI Symbol");
        }

        static BOOL CALLBACK setFontProc(HWND w, LPARAM lp) {
            SendMessageW(w, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
        }

        // The DPI of the window changed: make new fonts, give them to every control, free the old ones.
        void rebuildFonts() {
            HFONT oldUi = m_uiFont, oldBold = m_boldFont, oldStatus = m_statusFont;
            m_uiFont = m_boldFont = m_statusFont = nullptr;
            buildFonts();
            EnumChildWindows(m_wnd, setFontProc, reinterpret_cast<LPARAM>(m_uiFont));
            for (int i = 0; i < 5; ++i) if (m_rowLabel[i]) SendMessageW(m_rowLabel[i], WM_SETFONT, reinterpret_cast<WPARAM>(m_boldFont), TRUE);
            for (HWND st : { m_statusFlag, m_statusArtist, m_statusArtist2 })
                if (st) SendMessageW(st, WM_SETFONT, reinterpret_cast<WPARAM>(m_statusFont), TRUE);
            if (oldUi) DeleteObject(oldUi);
            if (oldBold) DeleteObject(oldBold);
            if (oldStatus) DeleteObject(oldStatus);
        }

        // The list's header sends its custom-draw notifications to the list itself, not to the page,
        // so the list is subclassed to give the header text a readable colour in dark mode.
        static LRESULT CALLBACK FieldListSubclassProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR refData) {
            auto* self = reinterpret_cast<country_flags_preferences_instance*>(refData);
            if (msg == WM_NOTIFY && self && self->isDarkMode()) {
                const NMHDR* nm = reinterpret_cast<const NMHDR*>(lp);
                if (nm && nm->code == NM_CUSTOMDRAW && nm->hwndFrom == ListView_GetHeader(wnd)) {
                    auto* cd = reinterpret_cast<NMCUSTOMDRAW*>(lp);
                    if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                    if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                        SetTextColor(cd->hdc, RGB(240, 240, 240));
                        return CDRF_NEWFONT;
                    }
                    return CDRF_DODEFAULT;
                }
            }
            if (msg == WM_NCDESTROY) RemoveWindowSubclass(wnd, FieldListSubclassProc, id);
            return DefSubclassProc(wnd, msg, wp, lp);
        }

        static BOOL CALLBACK destroyStrayListProc(HWND w, LPARAM lp) {
            wchar_t cls[32] = {};
            GetClassNameW(w, cls, 32);
            if (wcscmp(cls, WC_LISTVIEWW) == 0) DestroyWindow(w);
            (void)lp;
            return TRUE;
        }

        // Creates (or re-creates) the field list. Safe to call again when the old window is gone.
        void createFieldList() {
            // No leftover list windows (they would show as an unsized white box in the corner).
            if (m_tab3Wnd) EnumChildWindows(m_tab3Wnd, destroyStrayListProc, 0);
            m_fieldList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                0, 0, 10, 10, m_tab3Wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FIELDLIST)),
                core_api::get_my_instance(), nullptr);
            if (!m_fieldList) {
                const std::wstring msg = L"The field list could not be created (Windows error " + std::to_wstring(GetLastError()) + L").";
                if (m_fieldNote) SetWindowTextW(m_fieldNote, msg.c_str());
                return;
            }
            SetWindowSubclass(m_fieldList, FieldListSubclassProc, 2, reinterpret_cast<DWORD_PTR>(this));
            SendMessageW(m_fieldList, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);
            ListView_SetExtendedListViewStyle(m_fieldList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            const wchar_t* titles[3] = { L"Source", L"Field", L"Description" };
            for (int c = 0; c < 3; ++c) {
                LVCOLUMNW col{};
                col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                col.pszText = const_cast<LPWSTR>(titles[c]);
                col.cx = 100;
                col.iSubItem = c;
                SendMessageW(m_fieldList, LVM_INSERTCOLUMNW, c, reinterpret_cast<LPARAM>(&col));
            }
            applyFieldListColors();
        }

        // The list can have been lost (destroyed by something else): make a new one and fill it.
        void ensureFieldList() {
            if (m_fieldList && IsWindow(m_fieldList)) return;
            m_fieldList = nullptr;
            createFieldList();
            if (m_fieldList && m_fieldHint) SetWindowPos(m_fieldList, m_fieldHint, 0, 0, 10, 10, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }

        // ---- "Fields" tab: every available field per source, one copyable row each ----
        void createTab3Controls() {
            m_fieldNote = addLabel(m_tab3Wnd,
                L"Source 1 uses the fixed fields %countryflag_emoji%, %countryflag_path%, ...\n"
                L"Sources 2-5 use %countryflag_<suffix>_emoji% etc.\n"
                L"The fallback script runs only when the script returns no country; its result (an artist name)\n"
                L"is looked up in the artist tables on the Folders tab.");
            createFieldList();
            m_fieldHint = addLabel(m_tab3Wnd,
                L"Select one or more fields and press Ctrl+C, double-click a field, or use Copy. ");
            m_copyBtn = CreateWindowExW(0, L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, m_tab3Wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COPYFIELD)),
                core_api::get_my_instance(), nullptr);
            SendMessageW(m_copyBtn, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);
            EnableWindow(m_copyBtn, FALSE);
        }

        // Rebuilds the list from the suffixes currently on the Sources tab.
        void refreshFieldList() {
            ensureFieldList();
            if (!m_fieldList) return;
            static const int prefixIDs[] = { ID_PREFIX1, ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5 };
            SendMessageW(m_fieldList, WM_SETREDRAW, FALSE, 0);
            ListView_DeleteAllItems(m_fieldList);
            m_fieldTexts.clear();
            int row = 0;
            auto addRow = [&](const std::string& source, const std::string& field, const std::string& info) {
                pfc::stringcvt::string_wide_from_utf8 wSource(source.c_str());
                pfc::stringcvt::string_wide_from_utf8 wField(field.c_str());
                pfc::stringcvt::string_wide_from_utf8 wInfo(info.c_str());
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = row;
                item.iSubItem = 0;
                item.pszText = const_cast<LPWSTR>(wSource.get_ptr());
                const LRESULT at = SendMessageW(m_fieldList, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
                if (at < 0) return;
                LVITEMW sub{};
                sub.iSubItem = 1;
                sub.pszText = const_cast<LPWSTR>(wField.get_ptr());
                SendMessageW(m_fieldList, LVM_SETITEMTEXTW, static_cast<WPARAM>(at), reinterpret_cast<LPARAM>(&sub));
                sub.iSubItem = 2;
                sub.pszText = const_cast<LPWSTR>(wInfo.get_ptr());
                SendMessageW(m_fieldList, LVM_SETITEMTEXTW, static_cast<WPARAM>(at), reinterpret_cast<LPARAM>(&sub));
                m_fieldTexts.push_back(wField.get_ptr());
                ++row;
                };
            for (int i = 0; i < 5; ++i) {
                std::string prefix = "countryflag_";
                std::string label = "Source " + std::to_string(i + 1);
                if (i > 0) {
                    const std::string suffix = cleanSuffix(trim(readEdit(prefixIDs[i]).c_str()), i);
                    prefix += suffix + "_";
                    label += " (" + suffix + ")";
                }
                for (size_t k = 0; k < kFieldKindCount; ++k)
                    addRow(label, "%" + prefix + kFieldKindNames[k] + "%", kFieldKindInfo[k]);
            }
            addRow("All sources", "%countryflag_lookup%", "ISO2 code(s) of the track's artist(s) found in the artist tables");
            SendMessageW(m_fieldList, WM_SETREDRAW, TRUE, 0);
            applyFieldListColors();
            InvalidateRect(m_fieldList, nullptr, TRUE);
            if (m_copyBtn) EnableWindow(m_copyBtn, FALSE);
        }

        // Explicit list colours so the rows are readable in both light and dark mode.
        void applyFieldListColors() {
            if (!m_fieldList) return;
            const bool d = isDarkMode();
            // The theme must follow the current mode too (the mode can change while the page is open):
            // dark = dark list + header + scrollbars, light = the normal system look.
            SetWindowTheme(m_fieldList, d ? L"DarkMode_Explorer" : L"Explorer", nullptr);
            if (HWND hdr = ListView_GetHeader(m_fieldList)) SetWindowTheme(hdr, d ? L"DarkMode_ItemsView" : nullptr, nullptr);
            const COLORREF bk = d ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW);
            const COLORREF fg = d ? RGB(240, 240, 240) : GetSysColor(COLOR_WINDOWTEXT);
            SendMessageW(m_fieldList, LVM_SETBKCOLOR, 0, bk);
            SendMessageW(m_fieldList, LVM_SETTEXTBKCOLOR, 0, bk);
            SendMessageW(m_fieldList, LVM_SETTEXTCOLOR, 0, fg);
            InvalidateRect(m_fieldList, nullptr, TRUE);
        }

        // Copies the field text of the selected rows (one per line) to the clipboard.
        void copySelectedFields() {
            std::wstring text;
            int index = -1;
            while ((index = ListView_GetNextItem(m_fieldList, index, LVNI_SELECTED)) >= 0) {
                if (index >= (int)m_fieldTexts.size()) continue;
                if (!text.empty()) text += L"\r\n";
                text += m_fieldTexts[index];
            }
            if (text.empty() || !OpenClipboard(m_wnd)) return;
            EmptyClipboard();
            const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (mem) {
                if (void* dst = GlobalLock(mem)) {
                    memcpy(dst, text.c_str(), bytes);
                    GlobalUnlock(mem);
                    if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
                }
                else GlobalFree(mem);
            }
            CloseClipboard();
        }

        // Preview label + box: Sources tab only.
        void createPreviewControls() {
            HINSTANCE inst = core_api::get_my_instance();
            HGDIOBJ gui = uiFont();
            m_previewLabel = CreateWindowExW(0, L"STATIC", L"Preview:", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS,
                0, 0, 10, 10, m_tab1Wnd, nullptr, inst, nullptr);
            SendMessageW(m_previewLabel, WM_SETFONT, reinterpret_cast<WPARAM>(gui), TRUE);
            m_previewBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                0, 0, 10, 10, m_tab1Wnd, nullptr, inst, nullptr);
            SendMessageW(m_previewBox, WM_SETFONT, reinterpret_cast<WPARAM>(gui), TRUE);
        }

        // Import / Export buttons: Folders tab only.
        void createImportExportControls() {
            HINSTANCE inst = core_api::get_my_instance();
            HGDIOBJ gui = uiFont();
            m_ieLabel = addLabel(m_tab2Wnd, L"Settings file (all tabs):");
            m_importBtn = CreateWindowExW(0, L"BUTTON", L"Import...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, m_tab2Wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_IMPORT)), inst, nullptr);
            SendMessageW(m_importBtn, WM_SETFONT, reinterpret_cast<WPARAM>(gui), TRUE);
            m_exportBtn = CreateWindowExW(0, L"BUTTON", L"Export...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, m_tab2Wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EXPORT)), inst, nullptr);
            SendMessageW(m_exportBtn, WM_SETFONT, reinterpret_cast<WPARAM>(gui), TRUE);
        }

        static void setText(HWND w, const std::string& text) {
            pfc::stringcvt::string_wide_from_utf8 wide(text.c_str());
            SetWindowTextW(w, wide.get_ptr());
        }

        static std::string withCrLf(const std::string& text) {
            std::string out;
            out.reserve(text.size() + 16);
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) out += '\r';
                out += text[i];
            }
            return out;
        }

        // Evaluates the scripts as they are on the page (applied or not) for the selected track.
        void updatePreview() {
            if (!m_previewBox || !m_ready) return;
            static const int scriptIDs[] = { ID_SOURCE1, ID_SOURCE2, ID_SOURCE3, ID_SOURCE4, ID_SOURCE5 };
            static const int fallbackIDs[] = { ID_FALLBACK1, ID_FALLBACK2, ID_FALLBACK3, ID_FALLBACK4, ID_FALLBACK5 };
            std::string label = "Preview:";
            std::string body;
            const metadb_handle_ptr track = pickPreviewTrack();
            if (track.is_empty()) {
                body = "Select a track in the playlist (or start playback) to see a preview.";
            }
            else {
                label += " " + trackTitle(track.get_ptr());
                for (int i = 0; i < 5; ++i) {
                    if (i) body += "\n";
                    body += previewSourceLine(track.get_ptr(), i + 1, readEdit(scriptIDs[i]), readEdit(fallbackIDs[i]));
                }
            }
            if (label != m_lastPreviewLabel) { m_lastPreviewLabel = label; setText(m_previewLabel, label); }
            if (body != m_lastPreview) { m_lastPreview = body; setText(m_previewBox, withCrLf(body)); }
        }

        // ---- import / export of all settings (UTF-8 text, key=value lines)
        static std::string escapeValue(const std::string& text) {
            std::string out;
            for (char c : text) {
                if (c == '\\') out += "\\\\";
                else if (c == '\n') out += "\\n";
                else if (c != '\r') out += c;
            }
            return out;
        }

        static std::string unescapeValue(const std::string& text) {
            std::string out;
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    ++i;
                    out += text[i] == 'n' ? '\n' : text[i];
                }
                else out += text[i];
            }
            return out;
        }

        bool pickSettingsFile(bool save, std::wstring& path) {
            std::wstring filter = L"Country Flags settings (*.cfsettings)";
            filter += L'\0';
            filter += L"*.cfsettings";
            filter += L'\0';
            filter += L"All files (*.*)";
            filter += L'\0';
            filter += L"*.*";
            filter += L'\0';
            filter += L'\0';
            wchar_t buffer[MAX_PATH * 4] = {};
            if (save) wcscpy_s(buffer, L"foo_countryflags_settings.cfsettings");
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = GetAncestor(m_wnd, GA_ROOT);
            ofn.lpstrFilter = filter.c_str();
            ofn.lpstrFile = buffer;
            ofn.nMaxFile = (DWORD)(sizeof(buffer) / sizeof(wchar_t));
            ofn.lpstrTitle = save ? L"Export settings" : L"Import settings";
            ofn.lpstrDefExt = L"cfsettings";
            ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY
                | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
            const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
            if (!ok) return false;
            path = buffer;
            return true;
        }

        // Exports what is on the page now, including changes that are not applied yet.
        void exportSettings() {
            std::wstring path;
            if (!pickSettingsFile(true, path)) return;
            static const int scriptIDs[] = { ID_SOURCE1, ID_SOURCE2, ID_SOURCE3, ID_SOURCE4, ID_SOURCE5 };
            static const int fallbackIDs[] = { ID_FALLBACK1, ID_FALLBACK2, ID_FALLBACK3, ID_FALLBACK4, ID_FALLBACK5 };
            static const int prefixIDs[] = { ID_PREFIX1, ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5 };
            std::string data = "# Country Flags settings\n";
            data += "format=foo_countryflags-settings-1\n";
            for (int i = 0; i < 5; ++i) {
                const std::string n = std::to_string(i + 1);
                data += "source." + n + ".script=" + escapeValue(readEdit(scriptIDs[i])) + "\n";
                data += "source." + n + ".fallback=" + escapeValue(readEdit(fallbackIDs[i])) + "\n";
                if (i > 0) data += "source." + n + ".suffix=" + escapeValue(cleanSuffix(readEdit(prefixIDs[i]), i)) + "\n";
            }
            data += "flags.folder=" + escapeValue(readEdit(ID_FLAG_DIR)) + "\n";
            data += "table.file=" + escapeValue(readEdit(ID_ARTIST_FILE)) + "\n";
            data += "table.file2=" + escapeValue(readEdit(ID_ARTIST2_FILE)) + "\n";
            data += std::string("table.order=") + (orderSelection() == 1 ? "flat" : "world") + "\n";
            FILE* f = nullptr;
            const bool ok = _wfopen_s(&f, path.c_str(), L"wb") == 0 && f
                && fwrite(data.data(), 1, data.size(), f) == data.size();
            if (f) fclose(f);
            if (!ok) themed_message::show(GetAncestor(m_wnd, GA_ROOT), "Country Flags",
                "The settings file could not be written.", false, IDI_WARNING);
        }

        // Loads the settings into the page; they take effect when Apply is pressed.
        void importSettings() {
            std::wstring path;
            if (!pickSettingsFile(false, path)) return;
            HWND owner = GetAncestor(m_wnd, GA_ROOT);
            std::string data;
            {
                FILE* f = nullptr;
                if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
                    themed_message::show(owner, "Country Flags", "The settings file could not be read.", false, IDI_WARNING);
                    return;
                }
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
                fclose(f);
            }
            if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
                data.erase(0, 3);

            std::unordered_map<std::string, std::string> table;
            size_t pos = 0;
            while (pos < data.size()) {
                size_t end = data.find('\n', pos);
                if (end == std::string::npos) end = data.size();
                std::string line = data.substr(pos, end - pos);
                pos = end + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line[0] == '#') continue;
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                table[line.substr(0, eq)] = unescapeValue(line.substr(eq + 1));
            }
            const auto format = table.find("format");
            if (format == table.end() || format->second != "foo_countryflags-settings-1") {
                themed_message::show(owner, "Country Flags", "This is not a Country Flags settings file.", false, IDI_WARNING);
                return;
            }
            if (themed_message::show(owner, "Country Flags",
                "Replace the settings on this page with the imported settings?\n\nThey are applied when you press Apply.",
                true, IDI_QUESTION) != IDYES) return;

            static const int scriptIDs[] = { ID_SOURCE1, ID_SOURCE2, ID_SOURCE3, ID_SOURCE4, ID_SOURCE5 };
            static const int fallbackIDs[] = { ID_FALLBACK1, ID_FALLBACK2, ID_FALLBACK3, ID_FALLBACK4, ID_FALLBACK5 };
            static const int prefixIDs[] = { ID_PREFIX1, ID_PREFIX2, ID_PREFIX3, ID_PREFIX4, ID_PREFIX5 };
            auto get = [&](const std::string& key) -> const std::string* {
                const auto it = table.find(key);
                return it == table.end() ? nullptr : &it->second;
                };
            for (int i = 0; i < 5; ++i) {
                const std::string n = std::to_string(i + 1);
                if (const std::string* v = get("source." + n + ".script")) setEdit(scriptIDs[i], v->c_str());
                if (const std::string* v = get("source." + n + ".fallback")) setEdit(fallbackIDs[i], v->c_str());
                if (i > 0) if (const std::string* v = get("source." + n + ".suffix")) setEdit(prefixIDs[i], cleanSuffix(trim(v->c_str()), i).c_str());
            }
            if (const std::string* v = get("flags.folder")) setEdit(ID_FLAG_DIR, v->c_str());
            if (const std::string* v = get("table.file")) setEdit(ID_ARTIST_FILE, v->c_str());
            if (const std::string* v = get("table.file2")) setEdit(ID_ARTIST2_FILE, v->c_str());
            if (const std::string* v = get("table.order")) setOrder(*v == "flat" ? 1 : 0);
            updateStatus();
            updatePreview();
            if (m_callback.is_valid()) m_callback->on_state_changed();
        }

        HWND addEditorButton(int id) const {
            HWND b = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, m_tab1Wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                core_api::get_my_instance(), nullptr);
            SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);
            return b;
        }

        void createTab1Controls() {
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
                m_editBtn[i] = addEditorButton(ID_EDITSCRIPT_BASE + i);
                addEdit(m_tab1Wnd, fallbackIDs[i]);
                m_editBtn[5 + i] = addEditorButton(ID_EDITSCRIPT_BASE + 5 + i);
            }
            setEdit(ID_PREFIX1, kFixedText);
            EnableWindow(control(ID_PREFIX1), FALSE);

            m_note = addLabel(m_tab1Wnd,
                L"Changing a suffix asks to restart foobar2000 when you press Apply.");
            createPreviewControls();
        }

        void createTab2Controls() {
            m_flagLabel = addLabel(m_tab2Wnd, L"Flag image folder (empty = component's flags folder)");
            m_statusFlag = addStatus(m_tab2Wnd, ID_STATUS_FLAG);
            addEdit(m_tab2Wnd, ID_FLAG_DIR);
            addButton(m_tab2Wnd, ID_BROWSE);

            m_artist2Label = addLabel(m_tab2Wnd, L"Artist table worldMap.json: last item of val = country (empty = profile\\js_data\\worldMap.json, else next to the component)");
            m_statusArtist2 = addStatus(m_tab2Wnd, ID_STATUS_ARTIST2);
            addEdit(m_tab2Wnd, ID_ARTIST2_FILE);
            addButton(m_tab2Wnd, ID_ARTIST2_BROWSE);

            m_artistLabel = addLabel(m_tab2Wnd, L"Artist table country_codes.json: {\"artist\": \"CC\"} (empty = JSplitter package_data folder if the file is there, else next to the component)");
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
            SendMessageW(m_orderWorld, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);
            SendMessageW(m_orderFlat, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);
            createImportExportControls();
        }

        // Fields tab: note on top, list in the middle, hint and Copy button below.
        // Sized from the tab page's own client rectangle, so it is right whenever the page is resized or shown.
        void layoutFieldsTab() {
            if (!m_tab3Wnd) return;
            ensureFieldList();
            if (!m_fieldList) return;
            // The Fields page holds exactly four controls (note, list, hint, Copy); remove anything else
            // (a leftover unsized window shows up as a small white box in the corner).
            for (HWND c = GetWindow(m_tab3Wnd, GW_CHILD), next = nullptr; c; c = next) {
                next = GetWindow(c, GW_HWNDNEXT);
                if (c != m_fieldNote && c != m_fieldList && c != m_fieldHint && c != m_copyBtn) DestroyWindow(c);
            }
            RECT rc{};
            GetClientRect(m_tab3Wnd, &rc);
            const int tabW = rc.right - rc.left, tabH = rc.bottom - rc.top;
            if (tabW <= 0 || tabH <= 0) return;
            auto S = [&](int v) { return scale(v); };
            const int margin = S(12), gap = S(8);
            const int copyW = S(90), hintH = S(34), noteH3 = S(68);
            if (m_fieldNote) SetWindowPos(m_fieldNote, nullptr, margin, margin, (std::max)(S(100), tabW - 2 * margin), noteH3, SWP_NOZORDER | SWP_NOACTIVATE);
            const int listY = margin + noteH3 + gap;
            const int listH = (std::max)(S(60), tabH - listY - margin - hintH - gap);
            const int listW = (std::max)(S(100), tabW - 2 * margin);
            SetWindowPos(m_fieldList, nullptr, margin, listY, listW, listH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            const int sourceW = S(120), fieldW = S(260);
            const int infoW = (std::max)(S(120), listW - sourceW - fieldW - S(24));
            ListView_SetColumnWidth(m_fieldList, 0, sourceW);
            ListView_SetColumnWidth(m_fieldList, 1, fieldW);
            ListView_SetColumnWidth(m_fieldList, 2, infoW);
            const int rowY = listY + listH + gap;
            if (m_copyBtn) SetWindowPos(m_copyBtn, nullptr, tabW - margin - copyW, rowY + (hintH - S(26)) / 2, copyW, S(26), SWP_NOZORDER | SWP_NOACTIVATE);
            if (m_fieldHint) SetWindowPos(m_fieldHint, nullptr, margin, rowY, (std::max)(S(100), tabW - 2 * margin - copyW - gap), hintH, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        void layoutControls(int width, int height) {
            if (!m_wnd || !m_tabWnd || !m_tab1Wnd || !m_previewBox) return;
            if (m_fontDpi != dpiForWindow(m_wnd)) rebuildFonts();
            auto S = [&](int v) { return scale(v); };
            const int margin = S(12), gap = S(8), rowH = S(26), browseW = S(86), statusW = S(20);

            const int tabAreaH = (std::max)(S(100), height);

            SetWindowPos(m_tabWnd, nullptr, 0, 0, width, tabAreaH, SWP_NOZORDER | SWP_NOACTIVATE);

            RECT rcTab{ 0, 0, width, tabAreaH };
            TabCtrl_AdjustRect(m_tabWnd, FALSE, &rcTab);
            const int tabW = (std::max)(S(100), static_cast<int>(rcTab.right - rcTab.left));
            const int tabH = (std::max)(S(100), static_cast<int>(rcTab.bottom - rcTab.top));

            if (m_tab1Wnd) SetWindowPos(m_tab1Wnd, nullptr, rcTab.left, rcTab.top, tabW, tabH, SWP_NOZORDER | SWP_NOACTIVATE);
            if (m_tab2Wnd) SetWindowPos(m_tab2Wnd, nullptr, rcTab.left, rcTab.top, tabW, tabH, SWP_NOZORDER | SWP_NOACTIVATE);
            if (m_tab3Wnd) SetWindowPos(m_tab3Wnd, nullptr, rcTab.left, rcTab.top, tabW, tabH, SWP_NOZORDER | SWP_NOACTIVATE);

            layoutFieldsTab();

            // --- Layout Tab 1 (Sources): no scrolling; everything is sized to fit the page ---
            // Per source:  [Source N]  Suffix: [edit]
            //              Titleformat [edit]
            //              Fallback    [edit]
            const int nominalRowH = 22, nominalGap = 4, nominalGroupGap = 8, nominalNoteH = 20, nominalPreviewLabelH = 18, nominalPreviewH = 80, nominalTop = 8;
            const int nominalH = nominalTop + 5 * (3 * nominalRowH + 2 * nominalGap + nominalGroupGap) + nominalNoteH + nominalGap + nominalPreviewLabelH + nominalGap + nominalPreviewH + nominalTop;
            // Shrink vertical spacing (never below 70%) when the page is shorter than the nominal layout.
            const int pct = (std::max)(70, (std::min)(100, MulDiv(tabH, 100, S(nominalH))));
            auto V = [&](int v) { return MulDiv(S(v), pct, 100); };
            const int rowH1 = V(nominalRowH), rowGap = V(nominalGap), groupGap = V(nominalGroupGap), noteH = V(nominalNoteH);

            const int fullW1 = (std::max)(S(100), tabW - 2 * margin);
            const int captionW = S(84);
            const int xEdit = margin + captionW;
            const int editW = (std::max)(S(60), fullW1 - captionW);
            const int editBtnW = S(30);
            const int scriptEditW = (std::max)(S(40), editW - editBtnW - gap / 2);
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
                // Row 2: titleformat (+ "..." editor button)
                place(m_caption[i][1], margin, y1 + capDy, captionW - gap, capH);
                place(GetDlgItem(m_tab1Wnd, scriptIDs[i]), xEdit, y1, scriptEditW, rowH1);
                place(m_editBtn[i], xEdit + scriptEditW + gap / 2, y1, editBtnW, rowH1);
                y1 += rowH1 + rowGap;
                // Row 3: fallback (+ "..." editor button)
                place(m_caption[i][2], margin, y1 + capDy, captionW - gap, capH);
                place(GetDlgItem(m_tab1Wnd, fallbackIDs[i]), xEdit, y1, scriptEditW, rowH1);
                place(m_editBtn[5 + i], xEdit + scriptEditW + gap / 2, y1, editBtnW, rowH1);
                y1 += rowH1 + groupGap;
            }
            place(m_note, margin, y1, fullW1, noteH);
            y1 += noteH + rowGap;
            place(m_previewLabel, margin, y1, fullW1, V(nominalPreviewLabelH));
            y1 += V(nominalPreviewLabelH) + rowGap;
            place(m_previewBox, margin, y1, fullW1, (std::max)(V(nominalPreviewH), tabH - y1 - V(nominalTop)));

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
            y2 += S(40);
            const int ieW = S(90);
            if (m_ieLabel) SetWindowPos(m_ieLabel, nullptr, margin, y2, fullW2, S(18), SWP_NOZORDER | SWP_NOACTIVATE);
            y2 += S(22);
            if (m_importBtn) SetWindowPos(m_importBtn, nullptr, margin, y2, ieW, S(26), SWP_NOZORDER | SWP_NOACTIVATE);
            if (m_exportBtn) SetWindowPos(m_exportBtn, nullptr, margin + ieW + gap, y2, ieW, S(26), SWP_NOZORDER | SWP_NOACTIVATE);
        }

        void updateStatus() {
            if (!m_wnd) return;
            struct Item { int edit; int status; bool folder; const char* defaultName; int location; };
            static const Item items[3] = {
                { ID_FLAG_DIR, ID_STATUS_FLAG, true, "flags", kLocComponent },
                { ID_ARTIST2_FILE, ID_STATUS_ARTIST2, false, "worldMap.json", kLocJsData },
                { ID_ARTIST_FILE, ID_STATUS_ARTIST, false, "country_codes.json", kLocJSplitterPackage },
            };
            for (int i = 0; i < 3; ++i) {
                const std::string text = trim(readEdit(items[i].edit).c_str());
                const fs::path p = text.empty() ? defaultTablePath(items[i].defaultName, items[i].location) : expandPath(text);
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

        static int scriptIndex(int id) {
            if (id >= ID_SOURCE1 && id <= ID_SOURCE5) return id - ID_SOURCE1;
            if (id >= ID_FALLBACK1 && id <= ID_FALLBACK5) return 5 + (id - ID_FALLBACK1);
            return -1;
        }
        // Script boxes show the compiled (flattened) form; the real, possibly multi-line text is kept in m_full.
        std::string readEdit(int id) const {
            const int idx = scriptIndex(id);
            if (idx >= 0) return m_full[idx];
            return rawEditText(id);
        }
        std::string rawEditText(int id) const {
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
            const int idx = scriptIndex(id);
            if (idx >= 0) {
                m_full[idx] = value ? value : "";
                const std::string shown = flattenScript(m_full[idx]);
                pfc::stringcvt::string_wide_from_utf8 wide(shown.c_str());
                HWND e = control(id);
                m_settingScript = true;
                if (e) SetWindowTextW(e, wide.get_ptr());
                m_settingScript = false;
                return;
            }
            pfc::stringcvt::string_wide_from_utf8 wide(value);
            HWND edit = control(id);
            if (edit) SetWindowTextW(edit, wide.get_ptr());
        }

        void openScriptEditor(int slot) {
            static const int ids[10] = { ID_SOURCE1, ID_SOURCE2, ID_SOURCE3, ID_SOURCE4, ID_SOURCE5,
                ID_FALLBACK1, ID_FALLBACK2, ID_FALLBACK3, ID_FALLBACK4, ID_FALLBACK5 };
            if (slot < 0 || slot > 9) return;
            const std::wstring title = (slot < 5 ? L"Source " + std::to_wstring(slot + 1) + L" - Titleformat string"
                : L"Source " + std::to_wstring(slot - 4) + L" - Fallback string");
            std::string text = readEdit(ids[slot]);
            if (!editScriptDialog(m_wnd, title.c_str(), text, slot < 5 ? 0 : 1)) return;
            setEdit(ids[slot], text.c_str());
            updatePreview();
            if (m_ready && m_callback.is_valid()) m_callback->on_state_changed();
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
        HWND m_tab3Wnd;                     // "Fields" tab: copyable list of all fields
        HWND m_fieldList, m_fieldHint, m_copyBtn;
        HWND m_fieldNote = nullptr;               // note above the list on the Fields tab
        HWND m_ieLabel = nullptr;                 // caption of the Import / Export buttons (Folders tab)
        std::vector<std::wstring> m_fieldTexts;   // field text of every list row
        bool m_ready;
        int m_originalOrder;
        HWND m_note;
        HWND m_caption[5][3];   // per source: suffix / titleformat / fallback captions
        HFONT m_boldFont;
        HWND m_editBtn[10];                 // "..." buttons: slot 0-4 titleformat, 5-9 fallback
        mutable std::string m_full[10];     // full (multi-line) text of the ten scripts
        mutable bool m_settingScript;
        HWND m_previewLabel, m_previewBox;   // Sources tab
        HWND m_importBtn, m_exportBtn;       // Folders tab
        std::string m_lastPreview, m_lastPreviewLabel;
        HWND m_flagLabel, m_artistLabel, m_artist2Label;
        HWND m_orderLabel, m_orderWorld, m_orderFlat;
        HWND m_statusFlag, m_statusArtist, m_statusArtist2;
        HWND m_rowLabel[5];
        HFONT m_statusFont;
        HFONT m_uiFont = nullptr;             // font of the host preferences dialog, DPI-matched
        UINT m_fontDpi = 0;
        HWND m_parent = nullptr;
        HBRUSH m_brDark;
        COLORREF m_statusColor[3];
        fb2k::CCoreDarkModeHooks m_darkMode;
        fb2k::CCoreDarkModeHooks m_tab1DarkMode;
        fb2k::CCoreDarkModeHooks m_tab2DarkMode;
        fb2k::CCoreDarkModeHooks m_tab3DarkMode;
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
        static constexpr t_uint32 kKinds = 9;
        static_assert(kKinds == kFieldKindCount, "field kinds out of sync with the Fields tab");
        static constexpr t_uint32 kLookupIndex = 5 * kKinds;

        t_uint32 get_field_count() override { return kLookupIndex + 1; }

        void get_field_name(t_uint32 index, pfc::string_base& out) override {
            if (index == kLookupIndex) { out.set_string("countryflag_lookup"); return; }
            if (index > kLookupIndex) { out.reset(); return; }
            const char* const* suffixes = kFieldKindNames;
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
            const SourceResult resolved = resolveSource(handle, sourceIndex);
            if (kind == 7 || kind == 8) {
                // Debug fields: the raw script results, published even when no country was found.
                const std::string& raw = kind == 7 ? resolved.argument : resolved.fallbackArgument;
                if (raw.empty()) return false;
                out->write(titleformat_inputtypes::meta, raw.c_str(), raw.length());
                return true;
            }
            const std::vector<std::string>& countries = resolved.countries;
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