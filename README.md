# foo_countryflags

A foobar2000 component (x86 and x64) that turns country codes or names stored in your tags into flag-related title formatting fields.

## Title-formatting fields

The component exposes five sets of fields.

| Source | Default input script | Field names |
|---|---|---|
| Source 1 | `%country%` | `%countryflag_emoji%`, `%countryflag_path%`, `%countryflag_name%`, `%countryflag_iso2%`, `%countryflag_iso3%` |
| Source 2 | `$if2(%artistcountry%,%countryflag_lookup%)` | `%countryflag_artist_emoji%`, `%countryflag_artist_path%`, `%countryflag_artist_name%`, `%countryflag_artist_iso2%`, `%countryflag_artist_iso3%` |
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
- Source scripts are title-formatting expressions that return a country code or country name. For example, `%artistcountry%`. Source 2 falls back to the [artist lookup table](#artist-lookup-table) when the tag is empty. These defaults only apply to fresh settings; with existing settings, paste the new Source 2 script yourself. Sources 3–5 have empty scripts by default and need to be configured before they return country data.

## How a country value is interpreted

A source script should return a country alpha-2 code (`GR`), alpha-3 code (`GRC`) or English name (`Greece`). Common aliases such as `UK`, `Czech Republic` and `Russian Federation` are also understood. MusicBrainz-style bracketed values such as `[Europe]` and `[Worldwide]` work too.

### Artist lookup table

`artist_countries.json` maps artist names to countries, so tracks without an artist-country tag can still get a flag:

```json
{
   "abba": "SE",
   "ac/dc": "AU",
   "aftermath": "US-NJ"
}
```

- **Location:** by default the file sits next to the DLL, next to `countries.txt`. To keep it elsewhere (for example in your profile folder so updates never replace it), set the path under **Artist lookup table** in preferences, or use its Browse button. Changing the path takes effect immediately after Apply.
- It is exposed as the field `%countryflag_lookup%`, which returns the ISO2 code(s) of the track's artist(s). Use it in any source script, for example `$if2(%artistcountry%,%countryflag_lookup%)` (the default for Source 2).
- Keys are matched case-insensitively (including accented letters) with surrounding spaces ignored. `The Cult` also matches a `cult, the` key and the other way round.
- **Streams:** for the track that is playing, the artist from the stream's dynamic metadata (for example ICY `StreamTitle` on internet radio) is searched first, and updates when the stream's track changes. If it is not in the table, the tags of the stream entry are tried.
- Every value of the `artist` tag is looked up (multi-value tags work); if none of them is in the table, `album artist` is tried. A name such as `A & B` is looked up as a whole.
- Values may be an ISO2/ISO3 code or a country name. A region suffix such as `US-NJ` is accepted and ignored (only the country is used), and `XE` means Europe. Unknown codes (for example `XG`, `YU`) are skipped unless you add them to `countries.txt`.
- The file must be a flat JSON object of strings, UTF-8 with or without BOM. It is re-read automatically within a few seconds of being saved; no restart is needed. If it is missing or invalid, `%countryflag_lookup%` is empty.

#### Second table: `worldMap.json`

A second artist table is searched for artists that are not in `artist_countries.json`. It uses a different format: an array of objects whose `val` list ends with the country (any items before it, such as city or region, are ignored):

```json
[
  { "artist": "The Kinks", "val": ["London", "England", "United Kingdom"] },
  { "artist": "Abba", "val": ["Sweden"] }
]
```

- The last item of `val` is the country and can be a name, ISO2 or ISO3 code (`United States`, `US`, `USA`). Entries whose last item is not a recognised country are skipped.
- Default location: next to the DLL as `worldMap.json`; set another path under **Artist table 2** in preferences. Matching rules, streams and automatic reloading work as for the first table.
- `artist_countries.json` always wins; if an artist appears more than once in `worldMap.json`, the first entry is used. Both tables feed the same `%countryflag_lookup%` field.

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
3. Make sure `countries.txt`, `artist_countries.json` (optional) and a `flags` folder (optional) containing PNG images named by lowercase ISO2 code (`gr.png`, `us.png`, ...) sit next to the DLL:
   - 64-bit foobar2000: `user-components-x64\foo_countryflags\`
   - 32-bit foobar2000: `user-components\foo_countryflags\`

   The component looks for these files relative to its own DLL, so each architecture needs its own copy (the package below already includes both). If you use a custom flag folder in preferences, only `countries.txt` has to sit next to the DLL (and the optional `artist_countries.json` / `worldMap.json`, unless you set their locations in preferences).

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
- `artist_countries.json`: optional artist-name to country table (see [Artist lookup table](#artist-lookup-table)); a sample with about 4,300 artists is included in the zip.
- `worldMap.json`: optional second artist table (see [Second table](#second-table-worldmapjson)); a sample with about 4,100 artists is included in the zip.
- `flags\*.png`: not bundled. [flag-icons](https://github.com/lipis/flag-icons) (MIT) is one source; export its SVGs to PNG and keep its license file.


## Building

- Visual Studio 2022, C++17
- Build two configurations: **Win32** (x86) and **x64**
- Link against `foobar2000_SDK`, `foobar2000_component_client` and `pfc`, built for the same platform as the component
- Output name for both: `foo_countryflags.dll`

## License

MIT License
