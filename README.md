# foo_countryflags

A foobar2000 component (x86 and x64) that turns country codes or names stored in your tags into flag-related title formatting fields.

## Title-formatting fields

The component exposes five sets of fields.

| Source | Default input script | Field names |
|---|---|---|
| Source 1 | `%country%` | `%countryflag_emoji%`, `%countryflag_path%`, `%countryflag_name%`, `%countryflag_iso2%`, `%countryflag_iso3%` |
| Source 2 | `%artistcountry%` | `%countryflag_artist_emoji%`, `%countryflag_artist_path%`, `%countryflag_artist_name%`, `%countryflag_artist_iso2%`, `%countryflag_artist_iso3%` |
| Source 3 | Empty | `%countryflag_3_emoji%`, `%countryflag_3_path%`, `%countryflag_3_name%`, `%countryflag_3_iso2%`, `%countryflag_3_iso3%` |
| Source 4 | Empty | `%countryflag_4_emoji%`, `%countryflag_4_path%`, `%countryflag_4_name%`, `%countryflag_4_iso2%`, `%countryflag_4_iso3%` |
| Source 5 | Empty | `%countryflag_5_emoji%`, `%countryflag_5_path%`, `%countryflag_5_name%`, `%countryflag_5_iso2%`, `%countryflag_5_iso3%` |

Each set contains these values:

- `emoji`: flag emoji built from regional-indicator characters.
- `path`: full path to the flag image, only if the file exists.
- `name`: English country name.
- `iso2`: uppercase ISO 3166-1 alpha-2 code, e.g. `GR`.
- `iso3`: uppercase ISO 3166-1 alpha-3 code, e.g. `GRC`.

If no country can be determined for a source, that source's fields are empty, so `$if(%countryflag_iso2%,...)` works as expected.

### Source suffixes

Open **Preferences > Tools > Country Flags** to configure each source's title-formatting script and suffix. Each source is one row with two columns: the title-formatting script on the left and the field suffix on the right. The flag image folder has a **Browse...** button.

- **Source 1** keeps the original, unsuffixed field names (`%countryflag_emoji%`, `%countryflag_path%`, etc.). Its field names are fixed.
- **Sources 2–5** use the required `countryflag_` prefix plus the suffix entered by the user. Enter only the suffix value; do not type `countryflag_`.
- Example: entering `artist` produces `%countryflag_artist_emoji%`, `%countryflag_artist_path%`, `%countryflag_artist_name%`, `%countryflag_artist_iso2%`, and `%countryflag_artist_iso3%`.
- Default suffixes are `artist`, `3`, `4`, and `5` for Sources 2–5. Suffixes must not be empty.
- **Restart foobar2000 after changing suffixes.** The available title-formatting field names are enumerated by foobar2000 and do not update immediately when the suffix setting changes.
- Source scripts are title-formatting expressions that return a country code or country name. For example, `%artistcountry%`. Sources 3–5 have empty scripts by default and need to be configured before they return country data.

## How a country value is interpreted

A source script should return a country alpha-2 code (`GR`), alpha-3 code (`GRC`) or English name (`Greece`). Common aliases such as `UK`, `Czech Republic` and `Russian Federation` are also understood. MusicBrainz-style bracketed values such as `[Europe]` and `[Worldwide]` work too.

### Built-in aliases

These alternative names are built in and are matched case-insensitively, in addition to the names in `countries.txt`. Add more by editing `countries.txt` (for example by adding a line with the alias as the name).

| Alias | Resolves to | `iso2` |
|---|---|---|
| `United States of America` | United States | `US` |
| `Czech Republic` | Czechia | `CZ` |
| `Turkiye`, `Türkiye` | Turkey | `TR` |
| `Burma` | Myanmar | `MM` |
| `Ivory Coast` | Côte d'Ivoire | `CI` |
| `Cape Verde` | Cabo Verde | `CV` |
| `Vatican` | Vatican City | `VA` |
| `Macedonia` | North Macedonia | `MK` |
| `Swaziland` | Eswatini | `SZ` |
| `East Timor` | Timor-Leste | `TL` |
| `Russian Federation` | Russia | `RU` |
| `Korea` | South Korea | `KR` |
| `Great Britain`, `UK`, `England`, `Scotland`, `Wales` | United Kingdom | `GB` |
| `Holland` | Netherlands | `NL` |
| `Hellas` | Greece | `GR` |
| `European Union`, `Europe`, `XE` | Europe | `EU` |
| `World`, `Worldwide` | Worldwide | `XW` |

### Europe and Worldwide (built in)

These non-ISO values are built in, so they work even with an unmodified `countries.txt` (an entry in that file takes precedence):

| Input | `iso2` | `iso3` | `name` | `emoji` | `path` |
|---|---|---|---|---|---|
| `XE`, `EU`, `Europe`, `European Union` | `EU` | `EUR` | Europe | European Union flag | `flags\eu.png`, or `flags\xe.png` if that is missing |
| `XW`, `Worldwide`, `World` | `XW` | `XWW` | Worldwide | Globe (U+1F310) | `flags\xw.png` |

`XE` is folded into `EU`, so `XE/EU` in one tag yields a single Europe entry rather than two.

## Installation

1. Install `foo_countryflags.fb2k-component` via Preferences > Components > Install, or double-click it.
2. Restart foobar2000.
3. Make sure `countries.txt` and a `flags` folder (optional) containing PNG images named by lowercase ISO2 code (`gr.png`, `us.png`, ...) sit next to the DLL:
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

- **Emoji on Windows**: the default Windows emoji font shows flag emoji as two letters (e.g. "GR") instead of a flag. `%countryflag_emoji%` only renders real flags with a flag-capable font such as Twemoji Country Flags. For reliable results, use `%countryflag_path%` with PNG images.
- **No `$function()` syntax**: foobar2000 does not let components register global title formatting functions, only fields. A call like `$flag(%x%)` is therefore not possible; set the source tags in preferences instead.
- **Caching and restart**: changing source suffixes requires restarting foobar2000 so its title-formatting field list is updated. Flag image existence is checked again after about 30 seconds, so adding or deleting a flag image while foobar2000 is running is picked up within that time (or immediately if you change the flag folder setting).																																																																						  
- **Thread safety**: fields are evaluated from multiple threads. The component reads its data once, lazily, and only reads configuration afterwards.

## Data files

- `countries.txt`: one country per line in the format `ISO2|ISO3|Name`. It follows ISO 3166-1 and also includes Kosovo using the user-assigned code `XKX`. Edit it to add aliases or change names. Back it up before updating the component, as component updates may replace the file.
- `flags\*.png`: not bundled. [flag-icons](https://github.com/lipis/flag-icons) (MIT) is one source; export its SVGs to PNG and keep its license file.


## Building

- Visual Studio 2022, C++17
- Build two configurations: **Win32** (x86) and **x64**
- Link against `foobar2000_SDK`, `foobar2000_component_client` and `pfc`, built for the same platform as the component
- Output name for both: `foo_countryflags.dll`

## License

MIT License
