# Changelog

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

