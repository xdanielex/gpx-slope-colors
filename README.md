# gpx-slope-colors

Colour a GPX track by slope — **uphill in one colour, downhill in another** — following the
**direction of travel**, and get a single GPX file you can import into
[OsmAnd](https://osmand.net/) without a Pro subscription.

![preview](example/preview.svg)

*Made with the synthetic track in [`example/demo.gpx`](example/demo.gpx).*

---

## Why

OsmAnd can already colour tracks by slope natively (*Appearance → Colour → Slope*), and it does
it properly: the gradient is signed and follows the order of the points, so uphill and downhill
are distinguished rather than just "steep vs flat". **But that colouring mode requires OsmAnd
Pro.**

This script does the same job from the outside: it computes the gradient itself and writes the
colours *into* the GPX file, as one `<trk>` per homogeneous section with its own
`<osmand:color>`. Per-track colours stored in the file are honoured by the free version.

Side benefit: because every section is drawn as a plain solid colour, the **direction arrows
stay visible** — they disappear with any of OsmAnd's built-in non-solid colouring modes
([osmandapp/OsmAnd#21082](https://github.com/osmandapp/OsmAnd/issues/21082)).

It is not OsmAnd-specific in any deep way — the output is standard GPX 1.1 and opens anywhere,
the colours are simply ignored by apps that do not read the OsmAnd extensions.

## Requirements

- Python 3.8 or newer. **No external dependencies**, standard library only.
  (The optional GUI uses `tkinter`, and `tkinterdnd2` if you want drag and drop.)
- A GPX file that contains elevation data (`<ele>` tags).
- OsmAnd 4.9.10 or newer, if you want per-track colours preserved on import.

## Install

There is nothing to install — grab the single file:

```bash
curl -O https://raw.githubusercontent.com/USER/gpx-slope-colors/main/gpx_slope_colors.py
python3 gpx_slope_colors.py ride.gpx
```

Or clone the repository and run it in place.

## Usage

### Graphical app

```bash
python3 gpx_slope_colors_gui.py
```

Drop GPX files into the window, adjust the settings, press Convert. Drag and drop
needs the optional `tkinterdnd2` package (`pip install tkinterdnd2`); without it the
*Add files* button does the same job. On Linux you may also need `python3-tk`.

### Command line

```bash
python3 gpx_slope_colors.py ride.gpx
# -> writes ride_slope.gpx
```

That is usually all you need. The defaults are red uphill, green downhill, purple flat, at
maximum line width.

### Where the output goes

Without `-o`, the output file is written **next to the input file** — not in the folder you run
the command from — with the `_slope` suffix added:

| Command | Output |
|---|---|
| `... ride.gpx` | `ride_slope.gpx` |
| `... tracks/ride.gpx` | `tracks/ride_slope.gpx` |
| `... /home/me/Downloads/x.gpx` | `/home/me/Downloads/x_slope.gpx` |

With `-o` the path is exactly what you pass, relative to the current folder. Either way the
script prints `written: <path>` when it is done.

> An existing output file is **overwritten without asking**.

### Options

| Option | Default | Description |
|---|---|---|
| `gpx` | — | Input GPX file (the only required argument) |
| `-h`, `--help` | — | Full help with examples |
| `-o`, `--output` | `<name>_slope.gpx` | Output file (see above) |
| `--threshold PCT` | `1.5` | Gradient % below which a section counts as flat |
| `--window M` | `50` | Metres used to smooth elevations and compute the gradient |
| `--minlen M` | `80` | Minimum length in metres of a coloured section |
| `--uphill COLOR` | `red` | Uphill colour (name or `#rrggbb`) |
| `--downhill COLOR` | `green` | Downhill colour (name or `#rrggbb`) |
| `--flat COLOR` | `purple` | Flat colour (name or `#rrggbb`) |
| `--no-flat` | off | Drop the flat class: everything is uphill or downhill |
| `--split-files` | off | Write one file per class instead of a single one; ignores `-o` |
| `--width W` | `24` | Line width: `thin`, `medium`, `bold`, or `1`–`24` |
| `--no-arrows` | off | Do not show direction arrows |
| `--keep-waypoints` | off | Copy `<wpt>` waypoints from the input into the output |
| `--quiet` | off | Print errors only |
| `--colors` | — | List the available colour names and exit |
| `--version` | — | Print the version and exit |

### Examples

```bash
# basic, automatic output name
python3 gpx_slope_colors.py ride.gpx
# -> ride_slope.gpx

# colours by name, explicit output name
python3 gpx_slope_colors.py ride.gpx -o coloured.gpx --uphill orange --downhill lightblue

# hex codes
python3 gpx_slope_colors.py ride.gpx -o coloured.gpx --uphill "#ff8800" --downhill "#0044cc"

# two classes only, no "flat"
python3 gpx_slope_colors.py ride.gpx -o two_colours.gpx --no-flat

# long track, thinner line
python3 gpx_slope_colors.py ride.gpx -o long.gpx --window 100 --minlen 250 --width 16

# keep the waypoints from the original file
python3 gpx_slope_colors.py ride.gpx --keep-waypoints

# separate files per class (-o is ignored)
python3 gpx_slope_colors.py ride.gpx --split-files
# -> ride_uphill.gpx, ride_downhill.gpx, ride_flat.gpx

# list the colour names
python3 gpx_slope_colors.py --colors
```

### Colour names

Usable with `--uphill`, `--downhill` and `--flat`. Italian aliases are accepted too
(`rosso`, `verde`, `azzurro`, …). A `#rrggbb` hex code always works.

| | Name | Hex |
|---|---|---|
| 🔴 | `red` | `#e01b1b` |
| 🔴 | `darkred` | `#8b0000` |
| 🟠 | `orange` | `#ff7a00` |
| 🟡 | `yellow` | `#f2c200` |
| 🟢 | `green` | `#00a03c` |
| 🟢 | `lightgreen` | `#5ad45f` |
| 🟢 | `darkgreen` | `#00602a` |
| 🔵 | `lightblue` | `#00bfff` |
| 🔵 | `blue` | `#0a7ef0` |
| 🔵 | `darkblue` | `#00337f` |
| 🔵 | `cyan` | `#00e5e5` |
| 🟣 | `purple` | `#9b30d9` |
| 🟣 | `magenta` | `#c724b1` |
| 🟣 | `pink` | `#ff2d95` |
| 🟤 | `brown` | `#8b5a2b` |
| ⚪ | `gray` / `grey` | `#8c8c8c` |
| ⚪ | `lightgray` | `#c8c8c8` |
| ⚫ | `black` | `#1a1a1a` |
| ⚪ | `white` | `#ffffff` |

## Tuning

**`--threshold`** — raise it if you get too many tiny sections on nearly flat ground, lower it
to pick up gentle gradients.

| Activity | Suggested threshold |
|---|---|
| Road cycling | `1` |
| MTB / gravel | `1.5` |
| Hiking | `3` |

**`--window`** and **`--minlen`** control fragmentation. On long tracks the defaults produce too
many sections:

| Track length | Suggested |
|---|---|
| up to 20 km | defaults (`--window 50 --minlen 80`) |
| 20–50 km | `--window 80 --minlen 150` |
| over 50 km | `--window 100 --minlen 250` |

If the result looks choppy, `--minlen` is the knob to turn first.

## Importing into OsmAnd

1. Copy the output file to your phone.
2. OsmAnd → **My Places → Tracks → Import** (or just open the file with OsmAnd).
3. Show the track on the map. **Nothing to set under Appearance** — the colours are already
   inside the file.

Requires OsmAnd **4.9.10 or newer**. Older versions flattened multi-track GPX files to a single
colour on import; with those you had to use *import as separate tracks* or `--split-files`.

## How it works

1. Track points are read in the order they are stored, i.e. the direction of travel.
2. The elevation profile is smoothed with a distance-weighted moving average (`--window`) to
   remove GPS and barometric noise.
3. For each point the gradient is computed over a window centred on it, and classified as
   uphill / flat / downhill against `--threshold`.
4. Adjacent points of the same class are grouped into sections; sections shorter than
   `--minlen` are merged into their larger neighbour, then adjacent sections of the same class
   are joined.
5. The GPX is rewritten as consecutive `<trk>` elements, each carrying its own
   `<osmand:color>`, `<osmand:width>`, `<osmand:show_arrows>` and
   `<osmand:coloring_type>solid</osmand:coloring_type>`.

Each section is extended by one point past its end so that it shares the junction point with the
next one. Without this overlap the segment between the last point of a section and the first of
the next would be drawn by neither, leaving visible gaps at high zoom.

## Limitations

- **Elevation data is required.** Without `<ele>` tags the script exits with an error. Fix the
  track first (OsmAnd's altitude correction, or a DEM service).
- **Track only.** Routes (`<rte>`) are ignored; only `<trk>`/`<trkseg>`/`<trkpt>` is read.
- **Waypoints are dropped unless you pass `--keep-waypoints`.**
- Timestamps and elevations are preserved; any other per-point extension (heart rate, cadence,
  power…) is **not** carried over.
- The junction overlap means the output has slightly more track points than the input — one
  extra per section.

## Regenerating the example

```bash
python3 make_demo.py                                    # writes example/demo.gpx
python3 gpx_slope_colors.py example/demo.gpx \
        -o example/demo_slope.gpx --window 60 --minlen 150
python3 make_preview.py                                 # writes example/preview.svg
```

## Support this project

The code here is free and MIT-licensed — take it and use it.

If you would rather have it ready to go, there is a **pay-what-you-want package** on
[Gumroad](https://YOURNAME.gumroad.com/l/gpx-slope-colors): double-click launchers for
Windows, macOS and Linux, plus an illustrated PDF manual. The minimum is zero, so you
can take that too.

## License

[MIT](LICENSE).
