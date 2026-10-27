# foo_countryflags

A foobar2000 component (x86 and x64) that turns country codes or names stored in your tags into flag-related title formatting fields.

## Fields

| Field | Result | Example |
|---|---|---|
| `%countryflag_iso2%` | ISO 3166-1 alpha-2 code, upper case | `GR` |
| `%countryflag_iso3%` | ISO 3166-1 alpha-3 code, upper case | `GRC` |
| `%countryflag_name%` | English country name | `Greece` |
| `%countryflag_path%` | Full path to the flag image, only if the file exists | `...\flags\gr.png` |
| `%countryflag_emoji%` | Flag emoji built from regional-indicator characters | 🇬🇷 |

If no country can be determined, the field is empty, so `$if(%countryflag_iso2%,...)` works as expected.

## How the country is found

The component checks a list of tag fields in order and uses the first value it recognises. The default list is:

```
COUNTRYCODE;COUNTRY;ARTISTCOUNTRY;RELEASECOUNTRY
```

A tag value can be an alpha-2 code (`GR`), an alpha-3 code (`GRC`) or an English name (`Greece`).

Common aliases such as `UK`, `Czech Republic` and `Russian Federation` are also understood.
            {"united states of america", "US"}, {"czech republic", "CZ"}, {"turkiye", "TR"},
            {"türkiye", "TR"}, {"burma", "MM"}, {"ivory coast", "CI"}, {"cape verde", "CV"},
            {"vatican", "VA"}, {"macedonia", "MK"}, {"swaziland", "SZ"}, {"east timor", "TL"},
            {"russian federation", "RU"}, {"korea", "KR"}, {"great britain", "GB"},
            {"england", "GB"}, {"scotland", "GB"}, {"wales", "GB"}, {"uk", "GB"},
            {"holland", "NL"}, {"hellas", "GR"}

## Configuration

Preferences > Advanced > Tools > Country flags

- **Source tag fields**: semicolon-separated list of tags to check, first match wins.
- **Flag image folder**: leave empty to use the `flags` folder next to the component.

## Installation

1. Install `foo_countryflags.fb2k-component` via Preferences > Components > Install, or double-click it.
2. Restart foobar2000.
3. Make sure `countries.txt` and a `flags` folder containing PNG images named by lowercase ISO2 code (`gr.png`, `us.png`, ...) sit next to the DLL:
   - 64-bit foobar2000: `user-components-x64\foo_countryflags\`
   - 32-bit foobar2000: `user-components\foo_countryflags\`

   The component looks for these files relative to its own DLL, so each architecture needs its own copy (the package below already includes both). If you use a custom flag folder in preferences, only `countries.txt` has to sit next to the DLL.

## Usage examples

Text:

```
$if(%countryflag_iso2%,[%countryflag_iso2%] ,)%artist% - %title%
```

Image in a panel that can load images from a path (JScript Panel):

```js
var path = fb.TitleFormat('%countryflag_path%').Eval(true);
var img = path ? gdi.Image(path) : null;
```

## Notes

- **Emoji on Windows**: the default Windows emoji font shows flag emoji as two letters (e.g. "GR") instead of a flag. `%countryflag%` only renders real flags with a flag-capable font such as Twemoji Country Flags. For reliable results, use `%countryflag_path%` with PNG images.
- **No `$function()` syntax**: foobar2000 does not let components register global title formatting functions, only fields. A call like `$flag(%x%)` is therefore not possible; set the source tags in preferences instead.
- **Caching**: the source tag list and the existence of each flag image are cached. Each flag file check is repeated after about 30 seconds, so a flag you add or delete while foobar2000 is running is picked up within that time (or immediately if you change the flag folder setting).																																																																						  
- **Thread safety**: fields are evaluated from multiple threads. The component reads its data once, lazily, and only reads configuration afterwards.

## Data files

- `countries.txt`: one country per line in the format `ISO2|ISO3|Name`. It follows ISO 3166-1 and also includes Kosovo using the user-assigned code `XKX`. Edit it to add aliases or change names.If you add entries create a backup, component updates will wipe the file.
- `flags\*.png`: not bundled. [flag-icons](https://github.com/lipis/flag-icons) (MIT) is one source; export its SVGs to PNG and keep its license file.


## Building

- Visual Studio 2022, C++17
- Build two configurations: **Win32** (x86) and **x64**
- Link against `foobar2000_SDK`, `foobar2000_component_client` and `pfc`, built for the same platform as the component
- Output name for both: `foo_countryflags.dll`

## License

MIT License
