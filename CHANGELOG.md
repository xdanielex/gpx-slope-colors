# Changelog

## 3.0.0 — batch styling, and one file to import

### New

- **Batch Styler.** A second mode that applies OsmAnd appearance settings to a
  whole folder of GPX files without touching a single GPS point. It is meant
  for walks split into stages and for archives of tracks collected over the
  years: instead of opening each track on the phone and setting colour, width
  and arrows by hand, the settings are written into the files once.

  ```sh
  gpx-slope-colors --style stages --auto-color --width 12 --arrows on
  ```

  `--auto-color` gives every file a different colour; `--color NAME` makes them
  all the same. Every appearance setting OsmAnd reads is supported: width,
  arrows, start/finish markers, track splitting, activity type, waypoint icons
  and colours by `<type>`, and the free colouring modes. See `--style --help`.

- **Everything into one file to import — on by default, in both modes.**
  Importing ten stages used to mean ten imports and ten trips through the
  appearance menu. Now several inputs are merged into a single multi-track
  `all-tracks.gpx`, and each track keeps its own colour, because in GPX the
  colour lives inside `<trk><extensions>` and not on the file.

  One import, nothing to configure afterwards. Use `--separate-files` (or the
  checkbox in the app) for the previous one-output-per-input behaviour.
  `--merge-name` picks the name. Merging is skipped automatically when there is
  only one input, with `--split-files`, or with an explicit `-o`.

- **Two tabs in the Windows app.** *Slope colours* is unchanged; *Batch
  styling* is the new mode. The file list is shared, so the same selection
  feeds either one.

- **Waypoint icons from `<type>`.** With `--group-by-type` each waypoint group
  gets its own icon, colour and background, picked from OsmAnd's own icon set.

- **Distance markers and the 3D wall**, in both modes. `--markers 1000` puts a
  label along the track every kilometre; `--3d altitude` draws the curtain
  under the track that OsmAnd shows when the map is tilted into 3D. The wall
  colour defaults to `solid`, so it takes each track's own colour and
  reinforces the slope colouring rather than painting over it.

  Both are optional and off by default: unless you ask for them no tag is
  written, so converting a file does not stamp a setting onto it. In the app
  each has three states — leave alone, remove, or set a value.

  The wall works on the **free** OsmAnd, tested on the device: best with the map
  tilted into 3D, and visible in plain 2D at close zoom too.

### Fixed

- **A file with no elevation was dropped from the merged output.** Converting
  twenty tracks where five had no `<ele>` produced a merged file holding
  fifteen, with the other five gone. They are now kept, drawn in yellow and
  named `<track> - no elevation data`, and the log says why they are not
  coloured. Losing a track quietly is worse than showing an uncoloured one.

- **Line width was ignored on tracks recorded with a sensor.** The track's own
  `<extensions>` was located by taking the first one after `<trk>`, but in a
  file recorded with a heart rate or cadence sensor every `<trkpt>` carries one
  of those. The width was written inside the first track point, where OsmAnd
  does not look for it, so those tracks came out at the default width while
  everything else was correct. The search now stops at the first `<trkseg>`,
  where a track-level block has to be.

- **A stale file-level width or colour overrode the new one.** A GPX exported
  by OsmAnd carries `<osmand:width>` at the `<gpx>` level, and it won over the
  per-track value written here. The stale copy is now removed when those are
  set per track — while tags belonging to other tools, such as Garmin or
  Wikiloc, are left in place instead of being wiped with it.

- **The progress bar did not move while batch styling.** The whole batch ran
  inside a single call, so the bar sat at zero and jumped to full at the end.
  The engine now reports each file as it finishes.

- **Direction arrows could be silently ignored.** `show_arrows` was written
  inside `<trk><extensions>`, where OsmAnd does not read it; only a copy at the
  `<gpx>` level is honoured. Verified on the device: two files identical except
  for the position of the block, and only the `<gpx>`-level one drew the
  arrows. The tag is now emitted in both places.

- **Waypoint icons did not appear.** Declaring a group in
  `<osmand:points_groups>` is not enough — OsmAnd wants the icon on the
  waypoint itself, otherwise the point falls back to the default marker. The
  icon, colour and background are now written into each `<wpt><extensions>` as
  well. The icon names are also validated against OsmAnd's published list
  instead of being invented.

- **Waypoint group colours were all the same.** Each file restarted from the
  first colour of the palette, so across a run every group came out the same
  red. A group now keeps one colour for the whole run, and different groups get
  different ones.

- **Running twice over the same folder did the wrong thing.** The second pass
  picked up the files written by the first, producing `name_osmand_osmand.gpx`
  and renumbering every colour. Output files are now recognised and skipped,
  and the merged file never re-reads itself.

- **A merged file could be invalid XML.** Tracks coming from Garmin carry
  `gpxx:` tags; the merged root did not declare that prefix, so the result
  would not open. Namespaces from the source files are now carried over, and
  the foreign extensions are preserved.

### Notes

- A waypoint sitting exactly on the start or end of a track is covered by
  OsmAnd's start/finish marker. The tool now warns instead of leaving you to
  wonder why an icon is missing: `--start-finish off` shows it.
- In OsmAnd, import with **"import as one track"**. This matters most for the
  merged file, which is one file holding many tracks.
- `--coloring slope` needs an OsmAnd Pro subscription and is refused with an
  explanation. Colouring by slope without Pro is exactly what this program's
  slope mode does, and it stays free.

## 2.1.1 — sensor data, and one feature removed

### Fixed
- **Heart rate, cadence and power were being thrown away.** The parser read
  only `<ele>` and `<time>` from each track point and dropped everything else,
  so a ride recorded on a bike computer came back stripped of its sensor data.
  Per-point `<extensions>` are now carried across verbatim, with the `gpxtpx`
  and `gpxpx` namespaces declared in the header. This also preserves tags like
  `<osmand:speed>`, which the original Python implementation discarded.

### Removed
- **`--single-track`**, added in 2.1.0 for Strava and Komoot. Testing it against
  the input showed the output was semantically identical to the file fed in:
  same points, same elevations, same timestamps, same sensor data, no colours.
  It converted a file into itself. Those sites cannot display per-section
  colours at all — a track holds one colour — so the honest answer is to upload
  the original file, not a copy of it. The checkbox is gone from the app too.

## 2.1.0 — beyond OsmAnd
### New
- **Garmin colour support.** Every track now also carries
  `<gpxx:DisplayColor>` and `<gpxtrx:DisplayColor>`. Garmin's schema allows
  only 17 fixed colour names, so colours are matched to the nearest one in
  CIELAB space — plain RGB distance put our green on `DarkCyan`. Both prefixes
  are emitted because BaseCamp and the handhelds each read one and ignore the
  other.
- `--single-track`, for Strava and Komoot. **Removed again in 2.1.1** — see above.

### Fixed
- GUI layout: a new checkbox overlapped the output folder field.

### Unchanged
The OsmAnd output is byte-for-byte what it was, verified against the reference
implementation on a 120 km route: 39 of 39 track blocks identical.

