# Cerberus benchmarks in Power BI Desktop

1. Open **Transform data**. Create a Text parameter named `BenchmarkFolder` with the absolute path to the repository's `Benchmarks` directory. Create a blank query named `Benchmarks`, paste [Benchmarks.pq](Benchmarks.pq) into **Advanced Editor**, then **Close & Apply**. Refresh after adding captures. The query reads CSVs recursively and uses each relative file path as its run ID. [Microsoft folder connector](https://learn.microsoft.com/en-us/power-query/connectors/folder)
2. Use **New table** for the first four expressions in [Comparison.dax](Comparison.dax). Create a one-to-many, single-direction relationship from `Cameras[camera]` to `Benchmarks[camera]`. Leave Run A, Run B and Statistic disconnected. Use **New measure** on Benchmarks for each remaining expression. Format `Delta %` as Percentage. [Microsoft measure setup](https://learn.microsoft.com/en-us/power-bi/transform-model/desktop-tutorial-create-measures)
3. Build a **Compare runs** page with single-select slicers for `Benchmarks[renderer]`, `Benchmarks[scene]`, `Cameras[camera]`, both run tables' `Run` columns, and `Statistic[Statistic]`. Add a card for `Comparison status`. Add two tables using `Benchmarks[pass]` and `Benchmarks[type]` as rows and Run A ms, Run B ms, Delta ms and Delta % as values. Filter one to `Timing = GPU frame` and the other to `Timing = GPU pass`. Sort the pass table descending by Run B ms to surface the most expensive passes. Disable totals.
4. Build a **Run details** page with renderer, scene, camera and `Benchmarks[Run]` slicers. Use a table with Run, camera, pass, type and the exported mean, median, P95 and sample count. Set those numeric fields to **Don't summarize**. Add GPU, resolution, renderer mode, build and Git commit as run details if needed. Save the report as a PBIX.

Delta ms is B minus A in milliseconds. Delta % divides that difference by A and is blank for a zero baseline. Positive values indicate higher latency in B. Deltas require distinct runs, one camera, matching recorded capture settings and an unambiguous timing row on both sides. A missing pass stays blank for that run. The compatibility check matches scene, renderer, resolution, GPU model, driver, Vulkan version, present mode, build, warm-up and sample frame counts, geometry counts, and PathTracer mode when applicable. Git commit remains visible so different revisions can be compared under the same recorded settings.

## Check against the existing captures

The three files contain 45 timing rows in total. All use First Scene, Camera 1, denoised PathTracer, an RTX 3080 and 1920×1080. With the `002739` file as A and `004231` as B, the GPU FrameTotal should show the following values.

| Statistic | A (ms) | B (ms) | Delta (ms) | Delta (%) |
| --- | ---: | ---: | ---: | ---: |
| Mean | 15.5756 | 15.4972 | -0.0784 | -0.5034% |
| Median | 15.5392 | 15.4317 | -0.1075 | -0.6918% |
| P95 | 15.8996 | 15.8546 | -0.0450 | -0.2830% |

With Mean selected, PathTrace should sort first for B at 6.73251 ms, followed by SVGFTemporalSplit at 2.20195 ms.

## Data limitations

- The exporter computes FrameTotal by summing named GPU pass timings for each sampled frame. Use the exported FrameTotal row for its mean, median and P95. Those timings cover instrumented passes and can differ from end-to-end frame latency.
- The CSV contains per-camera, per-run aggregates only. Frame samples are unavailable for pooled medians, pooled P95 or uncertainty estimates. The report displays individual run statistics and suppresses multi-row timing totals.
- Camera labels, scene stems and geometry counts provide a partial workload check. Confirm camera pose and lens settings, scene revision, hardware identity and unrecorded renderer options before interpreting deltas. The three available files exercise one scene, camera and renderer. They show run-to-run variation under the same recorded setup.
- The exporter writes unquoted fields. Commas or line breaks in camera or pass names can break the format. Each CSV needs the T2 header, and files with incompatible structure can fail refresh. The exporter uses local timestamps with one-second precision and can overwrite same-second captures with the same scene and renderer.
- Power BI Desktop was unavailable in the execution environment. The fixture values were checked from the real CSVs with PowerShell. Power Query refresh, DAX evaluation and visuals still need a check in Desktop.
