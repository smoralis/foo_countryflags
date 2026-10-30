# foo_countryflags

A foobar2000 component (x86 and x64) that turns country codes or names stored in your tags into flag-related title formatting fields.

## Title-formatting fields

The component exposes five sets of fields, plus `%countryflag_lookup%` (see [Artist tables](#artist-tables)).

| Source | Default input script | Field names |
|---|---|---|
| Source 1 | `%country%` | `%countryflag_emoji%`, `%countryflag_path%`, `%countryflag_name%`, `%countryflag_iso2%`, `%countryflag_iso3%`, `%countryflag_region%`, `%countryflag_subregion%` |
| Source 2 | `%artistcountry%` (fallback: `$if3(%artist%,%album artist%)`) | `%countryflag_artist_emoji%`, `%countryflag_artist_path%`, `%countryflag_artist_name%`, `%countryflag_artist_iso2%`, `%countryflag_artist_iso3%`, `%countryflag_artist_region%`, `%countryflag_artist_subregion%` |
| Source 3 | Empty | `%countryflag_3_emoji%`, `%countryflag_3_path%`, `%countryflag_3_name%`, `%countryflag_3_iso2%`, `%countryflag_3_iso3%`, `%countryflag_3_region%`, `%countryflag_3_subregion%` |
| Source 4 | Empty | `%countryflag_4_emoji%`, `%countryflag_4_path%`, `%countryflag_4_name%`, `%countryflag_4_iso2%`, `%countryflag_4_iso3%`, `%countryflag_4_region%`, `%countryflag_4_subregion%` |
| Source 5 | Empty | `%countryflag_5_emoji%`, `%countryflag_5_path%`, `%countryflag_5_name%`, `%countryflag_5_iso2%`, `%countryflag_5_iso3%`, `%countryflag_5_region%`, `%countryflag_5_subregion%` |

Each set contains these values:

- `emoji`: flag emoji built from regional-indicator characters.
- `path`: full path to the flag image, only if the file exists.
- `name`: English country name.
- `iso2`: uppercase ISO 3166-1 alpha-2 code, e.g. `GR`.
- `iso3`: uppercase ISO 3166-1 alpha-3 code, e.g. `GRC`.
- `region`, `subregion`: cultural region of the country, e.g. `Europe` / `Southern Europe` (see [Cultural regions](#cultural-regions)).
- `argument`: the raw result of the source's titleformat script, exactly as evaluated (e.g. `%countryflag_artist_argument%`, or `%countryflag_argument%` for Source 1). Use it to debug long scripts.
- `fallbackargument`: the raw result of the source's fallback script (e.g. `%countryflag_artist_fallbackargument%`). It is empty when the fallback did not run because the main script already returned a country.

If no country can be determined for a source, that source's fields are empty, so `$if(%countryflag_iso2%,...)` works as expected. The two debug fields are the exception: `argument` and `fallbackargument` show what the scripts returned even when no country was found (they are empty only when the script itself returned nothing, or, for `fallbackargument`, did not run).

### Several countries in one value

A script result may list several countries, separated by `,`, `;`, `/` or `+` (for example `US+DE` or `GB, SE`). Each part is looked up separately, duplicates are dropped, and the fields list every country: `emoji` is space-separated (`🇺🇸 🇩🇪`), the other fields are separated by `; `, and `path` lists every flag image that exists.

If a part is not a recognised country, it is skipped. If *no* part is recognised, the source counts as having no country and the fallback script runs. For example `US+DE` was not recognised in earlier versions, so the fallback ran and its artist lookup could return a single flag; `+` is now treated as a separator, so both countries are returned. Check `%countryflag_<suffix>_argument%` to see what the script returned, and `%countryflag_<suffix>_fallbackargument%` to see whether the fallback ran.

### Source suffixes

Open **Preferences > Tools > Country Flags** to configure the component. The page has two tabs:

- **Sources**: one block per source, five blocks stacked on the page (no scrolling). Each block has three rows: the source name (**Source 1** to **Source 5**) with its **Suffix** box, then the **Titleformat** string, then the optional **Fallback** string (see [fallback scripts](#fallback-scripts)). The **...** button next to each script opens a larger, resizable editor for [long scripts](#long-scripts). A short help text below the last source repeats the rules below.
- **Folders**: the flag image folder and the two artist tables, each with a Browse button and a found / not found marker (see [Paths](#paths-and-markers)), plus the choice of which artist table wins.

- **Source 1** keeps the original, unsuffixed field names (`%countryflag_emoji%`, `%countryflag_path%`, etc.). Its field names are fixed.
- **Sources 2–5** use the required `countryflag_` prefix plus the suffix entered by the user. Enter only the suffix value; do not type `countryflag_`.
- Example: entering `artist` produces `%countryflag_artist_emoji%`, `%countryflag_artist_path%`, `%countryflag_artist_name%`, `%countryflag_artist_iso2%`, and `%countryflag_artist_iso3%`.
- Default suffixes are `artist`, `3`, `4`, and `5` for Sources 2–5. Only letters, digits and `_` are kept (any other character is removed), leading and trailing `_` are trimmed, and an empty suffix falls back to the default.
- **Restart foobar2000 after changing suffixes.** The available title-formatting field names are enumerated by foobar2000 and do not update immediately when the suffix setting changes.
- Source scripts are title-formatting expressions that return a country code or country name, for example `%artistcountry%`. The defaults only apply to fresh settings.

## How a country value is interpreted

A source script should return a country alpha-2 code (`GR`), alpha-3 code (`GRC`) or English name (`Greece`). Common aliases such as `UK`, `Czech Republic` and `Russian Federation` are also understood. MusicBrainz-style bracketed values such as `[Europe]` and `[Worldwide]` work too.

### Fallback scripts

Every source has an optional **fallback script**. It runs only when the source script returns no recognised country (an empty result, or a value that is not a country). The fallback script must return an **artist name**, which is then looked up in the [artist tables](#artist-tables). If the name is not found as a whole, it is split on ` & `, ` and `, ` versus `, ` vs. `, ` vs `, ` + `, `, `, `; `, ` / ` and ` x ` and the first part found wins, so `Simon & Garfunkel` can be an entry of its own while `Artist A & Artist B` falls back to Artist A.

Example for a group header: Source 1 = `$if3(%albumartistcountry%,%artistcountry%,%country%)`, fallback = your own album-artist expression. If the tags give no country, the country of that album artist is taken from the tables.

- The fallback may be any title-formatting expression, including long ones with `$puts`/`$get` and `$meta()`. It is evaluated for the track the field is requested for. For the playing track, `%isplaying%` is `1`, and for a playing stream `%artist%`, `%title%` and `$meta(artist,0)`, `$meta(title,0)` (and other fields present in the stream's dynamic metadata) return the live stream values, so expressions written for a now-playing display behave the same here. Expressions that rely on other title-formatting functions or fields set elsewhere (for example a custom `%stationswap%`) see exactly what the track entry provides.
- Results are cached per track and source for up to 10 seconds, and the cache is dropped earlier when the track's info, the settings, an artist table or the stream metadata change, so a row that shows several fields evaluates the scripts once.
- Do not use this component's own `%countryflag_*%` per-source fields inside a script (they are ignored to prevent endless recursion).
- Default: only Source 2 has a fallback, `$if3(%artist%,%album artist%)`. The field `%countryflag_lookup%` (ISO2 code(s) of the track's artist tags and the stream artist, looked up in the artist tables) remains available for use in any script, for example `$if2(%artistcountry%,%countryflag_lookup%)`.

### Long scripts

Click the **...** button next to a Titleformat or Fallback box to edit it in a resizable multi-line editor (Ctrl+A selects all; **OK** keeps the change, **Cancel** discards it).

The editor has a **Test on selected track** button. It evaluates the script as it is in the editor (it does not have to be saved or applied) for the selected playlist item, or for the playing track when nothing is selected, and shows in the result box:

- the track used, the raw result of the script, and
- for a Titleformat script, the countries it resolves to (or that none were recognised, so the fallback would run); for a Fallback script, the countries found for the returned artist name in the artist tables (or that the artist was not found).

For a playing stream the test sees the same dynamic (ICY) artist and title as the fields do. Long scripts can be written readably, with one part per line, indentation and `//` comment lines:

```
// country of the album artist
$if3(%albumartistcountry%,
     %artistcountry%,
     %country%)
```

Before a script is compiled, the component removes whole-line `//` comments, strips leading and trailing whitespace from every line and joins the lines. The box on the page shows this flattened form, which is exactly what gets compiled. Two things to keep in mind:

- Do not rely on line breaks inside quoted text; they are removed.
- The full multi-line text is stored in the settings. If you type directly into the box on the page, your typing replaces the stored text with the one-line form, so use the editor to keep the line structure.

### Artist tables

Two optional JSON files map artist names to countries, so tracks without a country tag can still get a flag. When an artist is in both files, the table chosen by the radio buttons at the bottom of the **Folders** tab wins; the other one is only used for artists the first does not contain. The default is **`worldMap.json` first**, because it can be corrected (for example by clicking on the map), while a generated table such as `country_codes.json` can only be corrected by editing the file. The choice applies right after Apply.

| File | Format |
|---|---|
| `worldMap.json` | array: `[{"artist": "Abba", "val": ["Stockholm", "Sweden"]}]`, last item of `val` is the country |
| `country_codes.json` | flat object: `{"abba": "SE", "aftermath": "US-NJ"}` |

- **Location:** by default `worldMap.json` is read from `profile\js_data\worldMap.json` (inside the foobar2000 profile folder) and, if it is not there, from next to the DLL. `country_codes.json` is read from next to the DLL. To keep them elsewhere set the path of each table in preferences (see [Paths](#paths-and-markers)). A changed path takes effect right after Apply.
- Keys are matched case-insensitively (including accented letters) with surrounding spaces ignored. `The Cult` also matches a `cult, the` key and the other way round.
- `worldMap.json`: the last item of `val` is the country and can be a name, ISO2 or ISO3 code (`United States`, `US`, `USA`); other items (city, region) and other keys are ignored. Entries whose last item is not a recognised country are skipped. If an artist appears more than once, the **last** entry wins.
- `country_codes.json`: values may be an ISO2/ISO3 code or a country name. A region suffix such as `US-NJ` is accepted and ignored, and `XE` means Europe. Unknown codes (for example `XG`, `YU`) are skipped unless you add them to `countries.txt`. If a key appears more than once, the last one wins.
- Both files are UTF-8 with or without BOM, and are re-read automatically within a few seconds of being saved; no restart is needed. A missing file simply contributes nothing; an invalid file keeps the previously loaded data.
- **Streams:** for the track that is playing, the artist from the stream's dynamic metadata (for example ICY `StreamTitle` on internet radio) is searched first by `%countryflag_lookup%`, and updates when the stream's track changes.
- `%countryflag_lookup%` returns the ISO2 code(s) of the track's artists: every value of the `artist` tag, or `album artist` when none is found.

### Paths and markers

All three path settings (flag folder and both artist tables) accept:

- `%fb2k_profile_path%`: the foobar2000 profile folder, e.g. `%fb2k_profile_path%\foo_uie_jsplitter\package_data\{GUID}\country_codes.json`. The same setting then works for portable and non-portable installs and for any username.
- `%fb2k_component_path%`: the folder of this component's DLL. `%fb2k_path%`: the foobar2000 program folder.
- Any Windows environment variable such as `%APPDATA%`.
- Relative paths: tried against the foobar2000 program folder first (as before, so an existing `profile\...` setting keeps working on portable installs), then against the profile folder.
- **Browse** writes a path inside the profile folder back as `%fb2k_profile_path%\...` automatically.

A marker left of each path box shows whether the entered file or folder exists: a green ✓ when it does, a red ✗ when it does not. An empty box checks the default location (`profile\js_data\worldMap.json`, then next to the DLL, for `worldMap.json`; next to the DLL for the other files); a grey ✗ means that default is missing too (fine if you do not use that file). The marker only checks that the file exists, not that its content is valid.

### Cultural regions

`%countryflag_region%` and `%countryflag_subregion%` (and `%countryflag_artist_region%`, `%countryflag_3_subregion%`, ... for the other sources) give the cultural region of the country, so a query on a region matches every country in it:

| Country | `region` | `subregion` |
|---|---|---|
| `SLE` (Sierra Leone) | Africa | West Africa |
| `GR` (Greece) | Europe | Southern Europe |
| `US` (United States) | America | North America |

```
%countryflag_subregion% IS Eastern Europe
%countryflag_region% IS Africa
%countryflag_subregion% HAS Maghreb
```

- If a source resolves to several countries, or a country belongs to several regions, the field lists each region once, separated by `; `. Use `HAS` rather than `IS` to match tracks that have other regions as well.
- The built-in table is the (revised) "cultural regions" table of the Music Graph country descriptors, using ISO 3166-1 alpha-3 codes. Its groupings apply as given: for example `Egypt` is `Africa` / `Maghreb`, `Turkey` is `Mashriq` / `Anatolia`, and `South Korea` is `Asia` / `East Asia`.
- **Russia belongs to two regions**: `region` is `Asia; Europe` and `subregion` is `North Asia; Eastern Europe`, so `%countryflag_subregion% HAS Eastern Europe` and `HAS North Asia` both match Russian artists, while `IS Eastern Europe` does not (use `HAS`).
- Europe (`EU`/`XE`) has the region `Europe` and no subregion; Worldwide has neither. `ANT` (Netherlands Antilles) is in the table but not in the built-in country list, so it is ignored.
- **Override or extend it** with an optional `regions.txt` next to the DLL, one `ISO|Region|Subregion` per line (ISO2 or ISO3 code, `#` starts a comment, the file is UTF-8). The first line for a country replaces its built-in entries and further lines for the same country add more regions; a line with an empty region and subregion removes the country's regions. Example:

```
# Turkey: replace the built-in entry, then add a second region
TUR|Mashriq|Anatolia
TUR|Europe|Southern Europe
# no region for Antarctica
ATA||
```

- `regions.txt` is read once at startup, like the optional `countries.txt`; restart foobar2000 after editing it.

Built-in regions and subregions:

| Region | Subregion | Countries |
|---|---|---|
| Antarctica | Antarctica | 1 |
| Africa | West Africa | 16 |
| Africa | Maghreb | 6 |
| Africa | Central Africa | 9 |
| Africa | East Africa | 8 |
| Africa | South Africa | 21 |
| Asia | Central Asia | 6 |
| Asia | East Asia | 7 |
| Asia | West Asia | 3 |
| Asia | South Asia | 16 |
| Asia | North Asia | 3 |
| Europe | Eastern Europe | 13 |
| Europe | Southern Europe | 9 |
| Europe | Central Europe | 9 |
| Europe | Western Europe | 10 |
| Europe | Northern Europe | 12 |
| Mashriq | Arabian Peninsula | 6 |
| Mashriq | Anatolia | 4 |
| Mashriq | Levant | 6 |
| Mashriq | Mesopotamia | 2 |
| America | Caribbean | 25 |
| America | North America | 8 |
| America | Central America | 8 |
| America | South America | 15 |
| Oceania | Australasia | 5 |
| Oceania | Melanesia | 5 |
| Oceania | Micronesia | 6 |
| Oceania | Polynesia | 13 |

### Built-in aliases

These alternative names are built in and are matched case-insensitively, in addition to the names of the built-in country list. Add more with a [`countries.txt`](#data-files) override file: a line `US|USA|America` makes `America` the name of the United States and keeps `United States` working as an alias. To add an alias without renaming, put its line first and the line with the name you want to keep after it (the last line for an ISO2 code sets the displayed name).

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
| `Aland Islands` | Åland Islands | `AX` |
| `St. Barts`, `St. Barths`, `St. Barth`, `Saint Barthelemy` | Saint Barthélemy | `BL` |
| `Bonaire` | Bonaire, Sint Eustatius and Saba | `BQ` |
| `Curacao` | Curaçao | `CW` |
| `European Union`, `Europe`, `XE` | Europe | `EU` |
| `World`, `Worldwide` | Worldwide | `XW` |

### Europe and Worldwide (built in)

These non-ISO values are built in, so they work without any `countries.txt` (an entry for the same code in that file takes precedence):

| Input | `iso2` | `iso3` | `name` | `emoji` | `path` |
|---|---|---|---|---|---|
| `XE`, `EU`, `Europe`, `European Union` | `EU` | `EUR` | Europe | European Union flag | `flags\eu.png`, or `flags\xe.png` if that is missing |
| `XW`, `Worldwide`, `World` | `XW` | `XWW` | Worldwide | Globe (U+1F310) | `flags\xw.png` |

`XE` is folded into `EU`, so `XE/EU` in one tag yields a single Europe entry rather than two.

## Installation

1. Install `foo_countryflags.fb2k-component` via Preferences > Components > Install, or double-click it.
2. Restart foobar2000.
3. Make sure the optional `countries.txt`, `worldMap.json` / `country_codes.json` and a `flags` folder containing PNG images named by lowercase ISO2 code (`gr.png`, `us.png`, ...) sit next to the DLL:
   - 64-bit foobar2000: `user-components-x64\foo_countryflags\`
   - 32-bit foobar2000: `user-components\foo_countryflags\`

   The component looks for these files relative to its own DLL, so each architecture needs its own copy (the package below already includes both). The country list is built into the DLL, so no data file is required. If you use a custom flag folder in preferences, nothing has to sit next to the DLL except the optional `countries.txt`, `country_codes.json` and `worldMap.json` (the last two unless you set their locations in preferences).

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

- `regions.txt`: optional, overrides/extends the built-in [cultural regions](#cultural-regions); not included in the zip.
- `countries.txt` (optional): the country list is **built into the component** (ISO 3166-1, plus Kosovo using the user-assigned code `XKX`). If this file exists next to the DLL it is read at startup as an override: one country per line in the format `ISO2|ISO3|Name`. A line for an ISO2 code that is already built in replaces its name and ISO3 code (the old name and ISO3 code keep working as aliases); a line with a new code adds a country. Restart foobar2000 after editing it. Back it up before updating the component, as component updates may replace the file. An unchanged copy of the full list has no effect, so you can delete it and keep only your own lines.
- `country_codes.json`: optional artist table (see [Artist tables](#artist-tables));
- `worldMap.json`: optional artist table (default location: `profile\js_data\worldMap.json`, otherwise next to the DLL) (see [Artist tables](#artist-tables)); by default it takes precedence over `country_codes.json`; 
- `flags\*.png`: not bundled. [flag-icons](https://github.com/lipis/flag-icons) (MIT) is one source; export its SVGs to PNG and keep its license file.


## Building

- Visual Studio 2022, C++17
- Build two configurations: **Win32** (x86) and **x64**
- Link against `foobar2000_SDK`, `foobar2000_component_client` and `pfc`, built for the same platform as the component
- Output name for both: `foo_countryflags.dll`

## License

MIT License
