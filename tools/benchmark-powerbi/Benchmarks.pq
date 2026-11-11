let
    Columns = {"camera","pass","type","mean_ms","median_ms","min_ms","max_ms","p95_ms","samples"},
    Header = Text.Combine(Columns, ","),
    Required = {"scene","renderer","resolution","gpu","vulkan_api_version","gpu_driver_version_raw",
        "present_mode","build_configuration","warmup_frames","sample_frames","total_triangle_count","mesh_instance_count"},
    Metadata = Required & {"path_tracer_mode","git_commit"},
    Parse = (content as binary, run as text) as table =>
        let
            Lines = Lines.FromBinary(content, QuoteStyle.None, false, 65001),
            HeaderIndex = List.PositionOf(Lines, Header),
            Preamble = if HeaderIndex < 0 then error Error.Record("Benchmark CSV", "Missing T2 header", run)
                else List.FirstN(Lines, HeaderIndex),
            Comments = List.Select(Preamble, each Text.StartsWith(_, "#") and Text.Contains(_, ":")),
            Keys = List.Transform(Comments, each Text.Trim(Text.BeforeDelimiter(Text.Range(_, 1), ":"))),
            Values = List.Transform(Comments, each Text.Trim(Text.AfterDelimiter(_, ":"))),
            Meta = Record.FromList(Values, Keys),
            Fields = Record.SelectFields(Meta, Metadata, MissingField.UseNull),
            Renderer = Fields[renderer],
            RequiredForRun = Required & (if Renderer = "PathTracer" then {"path_tracer_mode"} else {}),
            MatchValues = List.Transform(RequiredForRun, each Record.Field(Fields, _)),
            MatchKey = if List.AnyTrue(List.Transform(MatchValues, each _ = null or _ = "" or Text.Lower(_) = "unknown"))
                or Fields[scene] = "UnknownScene" then null
                else Binary.ToText(Json.FromValue(MatchValues), BinaryEncoding.Hex),
            Body = Text.Combine(List.Skip(Lines, HeaderIndex + 1), "#(lf)"),
            Raw = Csv.Document(Body, [Delimiter=",", Columns=Columns, Encoding=65001, ExtraValues=ExtraValues.Error]),
            Typed = Table.TransformColumnTypes(Raw, {
                {"camera", type text}, {"pass", type text}, {"type", type text},
                {"mean_ms", type number}, {"median_ms", type number}, {"min_ms", type number},
                {"max_ms", type number}, {"p95_ms", type number}, {"samples", Int64.Type}}, "en-US"),
            WithRun = Table.AddColumn(Typed, "Run", each run, type text),
            WithMeta = Table.AddColumn(WithRun, "Metadata", each Fields, type record),
            Expanded = Table.ExpandRecordColumn(WithMeta, "Metadata", Metadata),
            WithKey = Table.AddColumn(Expanded, "Compatibility", each MatchKey, type nullable text),
            WithKind = Table.AddColumn(WithKey, "Timing", each
                if [type] = "Frame" and [pass] = "FrameTotal" then "GPU frame"
                else if [type] = "CPU" then "CPU" else "GPU pass", type text)
        in
            WithKind,
    Root = Text.TrimEnd(BenchmarkFolder, {"\", "/"}) & "\",
    Files = Table.SelectRows(Folder.Files(Root), each Text.Lower([Extension]) = ".csv"),
    Parsed = List.Transform(Table.ToRecords(Files),
        each Parse([Content], Text.Range([Folder Path], Text.Length(Root)) & [Name])),
    Output = Table.Combine({#table(Columns & {"Run"} & Metadata & {"Compatibility","Timing"}, {})} & Parsed),
    TextColumns = {"Run","Compatibility","Timing"} & Metadata
in
    Table.TransformColumnTypes(Output, List.Transform(TextColumns, each {_, type text}) & {
        {"camera", type text}, {"pass", type text}, {"type", type text},
        {"mean_ms", type number}, {"median_ms", type number}, {"min_ms", type number},
        {"max_ms", type number}, {"p95_ms", type number}, {"samples", Int64.Type}}, "en-US")
