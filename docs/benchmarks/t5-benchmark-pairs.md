# T5 benchmark: paired Direct / Indirect captures

The Benchmark panel captures every saved Forward camera twice: Direct followed by
Indirect, with independent warm-up and sampling windows. The original submission
mode and profiler state are restored at completion or cancellation. The submission
selector is locked during capture. PathTracer retains one denoised capture per
camera and uses `mode=PathTracer`.

Defaults are 2560 x 1440, 120 warm-up frames and 300 samples per camera/mode.
Resolution remains configurable. Warm-up is clamped to at least frames-in-flight
plus one, so timestamp results from the previous camera/mode are discarded.
Camera movement and automatic viewport resizing remain suspended during capture.

CSV schema 2 retains the original nine timing columns and appends:

| Column | Meaning |
|---|---|
| `mode` | Direct, Indirect or PathTracer |
| `draw_calls` | Mean CPU draw API submissions per sampled frame for this pass |
| `default_draw_calls` | Submissions without masked classification |
| `masked_draw_calls` | Submissions from masked pipeline buckets |
| `scene_instances` | Total scene mesh instances |
| `scene_triangles` | Total scene triangles, including instancing |

An indirect batch counts as one API call regardless of its command count.
`FrameTotal` sums the graph passes' timings and draw submissions; it is not a
separate GPU timestamp around the entire frame. CPU rows leave draw counters
empty. `CPUFrameTime` remains the application frame delta, including waits and
presentation, rather than a measurement of command recording alone. Scene counts
are repeated context and must not be summed across passes. They are not per-pass
submitted-triangle counters. Text fields use CSV quoting. Presentation metadata
records the mode used during capture, before restoration.

Power Query accepts both original T2 files and schema 2. Old Forward captures get
`mode=Unknown`, since their submission mode was not recorded; old PathTracer files
get `mode=PathTracer`. The comparison kit uses disconnected Mode A and Mode B
selectors so Direct and Indirect can be compared inside the same capture file.

This increment automates the paired capture and exports counters. It does not
complete T5 Step 4: command-line `--benchmark`, dedicated recording/submission CPU
timings, memory snapshots and the three reviewed Bistro camera bookmarks remain
separate work. The final baseline, three repeated runs and close-up masked visual
acceptance are also still pending.

## Validation

- Editor Debug and Release builds passed.
- An isolated C++ harness compiled the production camera/mode progression,
  accumulation, CSV-writing body and cancellation methods with mocked renderer
  inputs. Two cameras exercised all four Forward combinations, warm-up exclusion,
  one-sample PathTracer captures, default/masked totals and restoring both original
  submission and profiler states. CSV round-trip checks passed for commas, quotes,
  embedded newlines, empty CPU counters and the two existing T2 fixtures. Test
  artifacts are under `intermediate/t5-benchmark-test*`.
- The live Forward panel showed 30 Direct submissions versus one Indirect
  submission per geometry pass in First Scene. A complete paired live export was
  not verified: desktop focus was unreliable and the test session entered
  PathTracer, hitting the query-pool reset failure already recorded in the Bistro
  inspection report. This is not a clean Vulkan-validation or baseline run.
- The test instance was closed and the pre-test scene, camera sidecar, material
  files, asset registry and layout were restored from backups.
- Updated Power Query and DAX have not been executed in Power BI Desktop.
