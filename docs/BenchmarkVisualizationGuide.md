# Cerberus benchmark visualization guide

## What exists, and what does not

The existing integration is a Power BI **import/query and comparison kit**, not a finished dashboard. No verified `.pbix`, `.pbit` or `.pbip` report is delivered. Installing Desktop does not automatically install this kit or create visuals. You create a report once using the steps below and save it as a PBIX.

Paths below are relative to the repository/worktree root. This work was prepared in `E:\Dev\Cerberus-Engine\.worktrees\benchmark-tooling-stress`, branch `codex/benchmark-tooling-stress`. The original checkout and its active renderer changes were left alone.

| File | Purpose |
| --- | --- |
| `tools/benchmark-powerbi/Benchmarks.pq` | Existing Power Query M script: imports T2 captures recursively, parses metadata, assigns run identity and compatibility keys. |
| `tools/benchmark-powerbi/Comparison.dax` | Existing four calculated tables and six measures for two-run comparisons. |
| `tools/benchmark-powerbi/README.md` | Earlier setup instructions; its reference to three captures describes the original fixture set. |
| `tools/benchmark-powerbi/prepare_example.py` | New standard-library Python script validating the two example captures and producing a flat import and expected arithmetic. |
| `tools/benchmark-powerbi/example/captures/*.csv` | Two real, unchanged Cerberus T2 captures copied from the original checkout. |
| `tools/benchmark-powerbi/example/Benchmarks-flat.csv` | Generated 30-row quick-start dataset, already stripped of T2 comment headers. |
| `tools/benchmark-powerbi/example/expected-results.json` | Independently calculated comparison values and expensive-pass ranking. |
| `docs/BenchmarkVisualizationGuide.md` | This guide. |

The three earlier kit files were untracked in the original checkout when inspected; they are preserved in the isolated visualization commit. No changes were made to `Cerberus-Editor/src/Panels/BenchmarkPanel.cpp`, its header, or other T2 implementation. This is an inventory of inspected files, not an attribution of renderer history to the visualization work.

## Understand a T2 capture

`BenchmarkPanel::WriteCSV()` writes `Benchmarks/<timestamp>_<scene>_<renderer>.csv`. Each capture begins with `# key: value` metadata, followed by:

```text
camera,pass,type,mean_ms,median_ms,min_ms,max_ms,p95_ms,samples
```

Each row is one aggregate for a camera and timing identity, not an individual frame. `FrameTotal` is the exported sum of instrumented GPU pass times per sample. `CPUFrameTime` uses application timestep; it is not CPU command-recording time. A lower time in milliseconds is better.

`Benchmarks.pq` finds the exact header, reads the metadata preceding it, parses the numeric body using `en-US` decimal conventions and appends all captures. It adds:

- `Run`: relative CSV path under the selected folder. Different subdirectories distinguish otherwise identical filenames.
- Metadata columns: scene, renderer, resolution, GPU, driver, build, Git revision, sample counts and more.
- `Timing`: `GPU frame`, `GPU pass`, or `CPU`.
- `Compatibility`: encoded metadata used to reject misleading comparisons. Missing required metadata makes it null.

## Quick start: see real results immediately

1. Open Power BI Desktop and create a blank report.
2. Choose **Home > Get data > Text/CSV**.
3. Select `tools/benchmark-powerbi/example/Benchmarks-flat.csv` in this worktree.
4. Choose **Transform Data**. If auto-detection used the wrong decimal locale, set the timing columns to **Decimal number > Using Locale > English (United States)**. Keep run, camera, pass, type and metadata as text; samples is a whole number. Do not use the 64-bit asset metadata as floating-point numbers.
5. Choose **Close & Apply**.
6. Add a **Table** visual with `Run`, `camera`, `pass`, `type`, `mean_ms`, `median_ms`, `p95_ms`, `samples`. Set numeric columns to **Don't summarize**.
7. Filter to one run and `type` other than `CPU` and `Frame`. Sort `mean_ms` descending.

For the `004231` capture, `PathTrace` should be first at **6.73251 ms**, followed by `SVGFTemporalSplit` at **2.20195 ms**. This route is a quick standalone table; it does not supply the `Compatibility` and `Timing` columns required by the DAX kit. Use the full setup below for comparisons.

## Full setup: reusable folder import

1. Start a separate blank report (avoid mixing the quick-start table with the full model).
2. Open Power Query using **Transform data**, or **Get data > Blank query**.
3. In Power Query, select **Manage Parameters > New Parameter**. Name it exactly `BenchmarkFolder`, choose **Text**, and set Current Value to the absolute folder path.
4. To reproduce the supplied example, use:
   `E:\Dev\Cerberus-Engine\.worktrees\benchmark-tooling-stress\tools\benchmark-powerbi\example\captures`
5. For live captures from your existing editor instead use:
   `E:\Dev\Cerberus-Engine\Benchmarks`
6. Create **New Source > Blank Query**, name it exactly `Benchmarks`, open **Advanced Editor**, and replace all its contents with `tools/benchmark-powerbi/Benchmarks.pq`.
7. Accept the editor and inspect the preview. The example folder should produce **30 rows**, two runs, and Camera 1. Choose **Close & Apply**.

Use a folder containing only original T2 captures. Do not point this query at the parent `example` directory: its generated flat CSV is not T2 format and would fail the strict parser. Do not paste M code into the DAX formula bar.

Microsoft documents [Power Query and its Advanced Editor](https://learn.microsoft.com/en-us/power-bi/transform-model/desktop-query-overview). Ribbon names can differ with Desktop language and the new query editor preview.

## Add comparison tables and measures

Open `tools/benchmark-powerbi/Comparison.dax`. It contains several independent expressions; do not paste the whole file as one formula.

1. Use **Modeling > New table** four times, once per expression: `Run A`, `Run B`, `Cameras`, `Statistic`.
2. In Model view create a **one-to-many**, **single-direction**, active relationship from `Cameras[camera]` to `Benchmarks[camera]`.
3. Leave `Run A`, `Run B` and `Statistic` disconnected. Delete any automatically inferred relationships involving them. They are selectors whose values the measures read, not joined dimensions.
4. Select `Benchmarks`, then **New measure** six times, once for each remaining expression: `Timing ms`, `Run A ms`, `Run B ms`, `Comparison status`, `Delta ms`, `Delta %`.
5. Format `Delta %` as Percentage; use at least two decimal places for timings and more when investigating small differences.

Microsoft describes [creating measures in Desktop](https://learn.microsoft.com/en-us/power-bi/transform-model/desktop-measures).

## Build a Compare runs page

1. Add single-select slicers for `Benchmarks[renderer]`, `Benchmarks[scene]`, `Cameras[camera]`, `Run A[Run]`, `Run B[Run]`, and `Statistic[Statistic]`.
2. Add a **Card** showing `Comparison status`.
3. Add a **Table** with `Benchmarks[pass]`, `Benchmarks[type]`, `[Run A ms]`, `[Run B ms]`, `[Delta ms]`, `[Delta %]`. Filter this visual to `Benchmarks[Timing] = GPU frame`.
4. Duplicate the table, change its visual filter to `GPU pass`, and sort `[Run B ms]` descending. Disable totals on both tables.
5. Optionally add a third table filtered to `CPU`, keeping CPU and GPU results separate.
6. Select one scene, renderer and camera; choose different runs in A and B and choose Mean, Median or P95.
7. Save as a PBIX, for example `CerberusBenchmarks.pbix`. Only this step creates your actual Desktop report.

Do not add an ordinary `Benchmarks[Run]` slicer to this comparison page: selecting one run there also filters the measure context and can blank the other run. Use the disconnected A/B slicers. A separate Run details page can use the ordinary Run slicer.

The measures deliberately return blank for ambiguous timing rows. They do not sum pre-aggregated medians/P95 values or unrelated passes. The delta is **B minus A**; negative means B is faster. `Delta % = (B-A)/A`; zero baseline gives blank.

## Working example and expected results

Choose Camera 1, First Scene, PathTracer, and:

- A: `2026-11-11_002739_First Scene_PathTracer.csv`
- B: `2026-11-11_004231_First Scene_PathTracer.csv`

Both captures record Release, denoised PathTracer, RTX 3080, 1920x1080, 315,832 scene triangles, 30 mesh instances, 60 warm-up frames and 300 samples.

| FrameTotal statistic | A ms | B ms | B-A ms | Change |
| --- | ---: | ---: | ---: | ---: |
| Mean | 15.5756 | 15.4972 | -0.0784 | -0.5034% |
| Median | 15.5392 | 15.4317 | -0.1075 | -0.6918% |
| P95 | 15.8996 | 15.8546 | -0.0450 | -0.2830% |

These are run-to-run differences, not evidence of an optimization or of Forward/indirect performance. The supplied script recomputes them:

```powershell
py -3 tools/benchmark-powerbi/prepare_example.py
```

CSV parsing, row identity and arithmetic were executed successfully. Power Query refresh, DAX evaluation and visual rendering were **not** executed in Desktop in this session.

## Add captures and compare future runs

1. Use the existing editor Benchmark panel to capture fixed cameras. Preserve the scene/camera/configuration used for the baseline.
2. Keep each original CSV under the selected `BenchmarkFolder` or a subdirectory. Do not edit its header. For A/B trials, use folders such as `Direct` and `Indirect` to preserve the unrecorded submission mode.
3. In Desktop choose **Home > Refresh**. The query discovers new CSVs; no manual append is needed.
4. Select the new run in A or B. If the card says settings differ, inspect metadata rather than overriding the guard.
5. Before claiming a change, verify camera pose/FOV, scene revision, renderer options and hardware conditions manually. The CSV does not record all of them.
6. To move the capture folder, edit the `BenchmarkFolder` parameter and refresh. Moving/renaming files changes their Run IDs.

## Limitations and troubleshooting

- No verified report/dashboard file is included; the delivered scripts are a report-building kit. Desktop installation alone cannot validate M or DAX.
- The original checkout currently has four PathTracer captures, while the earlier README described three. The included example intentionally freezes two captures, totaling 30 rows.
- The folder query is strict: an unrelated CSV, renamed header, duplicate metadata key or malformed row can fail refresh. Inspect the error and move incompatible files out of the source folder.
- T2 exports unquoted text. Commas/newlines in camera or pass names are unsafe. Captures with identical scene/renderer timestamps within one second can overwrite each other.
- Compatibility checks recorded settings, not complete workload identity. Submission mode, lighting/quality settings, exact camera pose, scene content hash, clocks and background load are not all recorded. Different Git commits are intentionally allowed.
- Missing metadata can display raw timings while suppressing deltas. Missing passes remain blank rather than being treated as zero.
- Samples are aggregates only. You cannot reconstruct frame-time distributions, pooled P95, statistical significance, or confidence intervals from them.
- GPU FrameTotal covers instrumented passes; CPUFrameTime includes application scheduling/waits. Neither is a dedicated CPU draw-recording metric.
- Example paths must be changed if your checkout is elsewhere. Avoid loading both originals and copies of the same capture when analyzing a larger folder.
