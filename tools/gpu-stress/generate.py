"""Generate deterministic Forward scene assets; Python 3.10+, no packages.

Only creates scene/sidecar/manifest files in its output directory. Never edits
the asset registry, renderer, existing scenes or T2 implementation.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def registry(repo):
    text = (repo / "Cerberus-Editor/Resources/AssetRegistry.cregistry").read_text(encoding="utf-8-sig")
    return {path.replace("\\", "/"): (int(handle), kind) for handle, path, kind in re.findall(
        r"- Handle: (\d+)\s+FilePath: ([^\r\n]+)\s+Type: (\w+)", text)}


def inputs(repo):
    assets = json.loads((HERE / "assets.json").read_text())
    entries = registry(repo)
    paths = {name: f"Resources/Meshes/Default/{name}.cmesh" for name in assets}
    paths.update(Material="Resources/Materials/Defaults/Cylinder/Cylinder_DefaultMaterial.cmaterial",
                 Environment="Resources/Environment/pink_sunrise_4k.hdr")
    handles = {}
    for name, path in paths.items():
        handle, kind = entries[path]
        if not (repo / "Cerberus-Editor" / path).is_file():
            raise ValueError(f"Missing asset: {path}")
        expected = "Mesh" if name in assets else "Material" if name == "Material" else "EnvMap"
        if kind != expected:
            raise ValueError(f"Unexpected asset type: {path}: {kind}")
        handles[name] = handle
    for name, asset in assets.items():
        source = repo / asset["source"]
        if hashlib.sha256(source.read_bytes()).hexdigest() != asset["sha256"]:
            raise ValueError(f"Changed source {source}; re-run inspect_assets.py before trusting counts")
        mesh_text = (repo / "Cerberus-Editor" / paths[name]).read_text()
        source_handle = int(re.search(r"MeshSource: (\d+)", mesh_text)[1])
        source_path = asset["source"].removeprefix("Cerberus-Editor/")
        if entries[source_path] != (source_handle, "MeshSource"):
            raise ValueError(f"Mesh/source registry mismatch: {name}")
    return assets, handles


def camera(name, position, target, fov=65):
    dx, dy, dz = [b-a for a, b in zip(position, target)]
    return dict(Name=name, Position=position, Yaw=math.degrees(math.atan2(dz, dx)),
                Pitch=math.degrees(math.atan2(dy, math.hypot(dx, dz))), FOV=fov, Near=0.2, Far=6000)


def cell_parts(district):
    # Asset extents after import: Cube/Cylinder/Cone are unit size, Torus is
    # 1 x .2 x 1, horizontal. X=90 turns portal rings into the XY plane.
    parts = [("Cube", (0, -.3, 0), (17, .6, 17), (0, 0, 0), "Paving")]
    for x in (-6, 6):
        for z in (-6, 6):
            parts.append(("Cylinder", (x, 3, z), (.9, 6, .9), (0, 0, 0), "Column"))
            parts.append(("Cube", (x, 6.2, z), (1.6, .4, 1.6), (0, 0, 0), "Capital"))
    for z in (-6, 6):
        parts.extend([("Torus", (0, 4.3, z), (7, 1.5, 7), (90, 0, 0), "Portal"),
                      ("Cube", (0, 7, z), (13, .7, 1), (0, 0, 0), "Entablature"),
                      ("Cone", (6, 7.4, z), (2, 2, 2), (0, 0, 0), "Finial")])
    for x in (-4, -1.3, 1.3, 4):
        parts.append(("Cube", (x, 1, 7), (.35, 2, .35), (0, 0, 0), "Balustrade"))
    parts.append(("Cube", (-8, 4, 0), (.8, 8, 17), (0, 0, 0), "Occluder"))
    assert len(parts) == 20
    if district == "B":
        # Small architectural details dominate this area's draw count.
        parts = [("Cube" if mesh in ("Cylinder", "Cone") else mesh, pos,
                  (.35, 2, .35) if mesh == "Cylinder" else scale, rot, label)
                 for mesh, pos, scale, rot, label in parts]
    if district == "C":
        parts[-1] = ("Cube", (0, 4, 7.8), (17, 8, .8), (0, 0, 0), "Occluder")
    return parts


def generate(repo, output, preset, count):
    assets, handles = inputs(repo)
    name = "GPUStressTest" if preset == "Low" else f"GPUStressTest_{preset}"
    scene_path = output / f"{name}.cscene"
    cells = math.ceil(count / 20)
    width = math.ceil(math.sqrt(math.ceil(cells / 3)))
    district_span = width * 18
    separation = district_span + 100
    counts, by_district = Counter(), {d: Counter() for d in "ABC"}
    entities = []

    def entity(label, position=(0, 0, 0), scale=(1, 1, 1), rotation=(0, 0, 0)):
        return dict(Entity=8000000000000000000+len(entities),
                    RelationshipComponent=dict(Parent=0, Children=[]), TagComponent=dict(Tag=label),
                    TransformComponent=dict(Translation=list(position), Rotation=list(rotation), Scale=list(scale)))

    light = entity("GPUStressTest Directional", rotation=(-35, -30, 0))
    light["DirectionalLightComponent"] = dict(Radiance=[1, .97, .92], Intensity=3)
    entities.append(light)
    sky = entity("GPUStressTest Sky")
    sky["SkyLightComponent"] = dict(AssetHandle=handles["Environment"], Intensity=.6, SampleLOD=1)
    entities.append(sky)
    emitted = 0
    for cell in range(cells):
        district = "ABC"[cell % 3]
        local = cell // 3
        x = (local % width - (width-1)/2)*18 + (ord(district)-ord("A"))*separation
        z = -(local // width)*18
        for mesh, pos, scale, rot, label in cell_parts(district):
            if emitted == count:
                break
            placed = [round(x+pos[0], 6), pos[1], round(z+pos[2], 6)]
            item = entity(f"{district} {label} {emitted:06d}", placed, scale, rot)
            item["MeshComponent"] = dict(AssetHandle=handles[mesh], SubmeshIndex=-1,
                                         MaterialTable={"0": handles["Material"]})
            entities.append(item)
            counts[mesh] += 1
            by_district[district][mesh] += 1
            emitted += 1
    cameras = [camera("A Geometry Throughput", [0, district_span*.65+20, district_span*.65+25],
                      [0, 2, -district_span*.4], 75),
               camera("B Draw Call Overhead", [separation, 3, 12], [separation, 3, -district_span], 65),
               camera("C Visibility Occlusion", [separation*2, 2, 16], [separation*2+3, 3, -district_span], 55)]
    scene = dict(Scene=name, Renderer="Forward", EditorCamera={k: v for k, v in cameras[0].items()
                                                               if k in ("Position", "Yaw", "Pitch")}, Entities=entities)
    output.mkdir(parents=True, exist_ok=True)
    # JSON is a YAML-compatible flow format accepted by yaml-cpp. Keep one
    # entity per line for useful diffs without 100k entities of indentation.
    prefix = json.dumps({k: v for k, v in scene.items() if k != "Entities"})[:-1]
    text = prefix + ',"Entities":[\n' + ',\n'.join(json.dumps(e, separators=(",", ":")) for e in entities) + '\n]}\n'
    scene_path.write_text(text, encoding="utf-8", newline="\n")
    sidecar_path = scene_path.with_suffix(".benchmark.yaml")
    sidecar_path.write_text(json.dumps(dict(Cameras=cameras), indent=2)+"\n", encoding="utf-8", newline="\n")
    triangles = sum(counts[n]*assets[n]["triangles"] for n in counts)
    unique_vertex = sum(assets[n]["vertices"]*56 for n in counts)
    unique_index = sum(assets[n]["triangles"]*12 for n in counts)
    manifest = dict(scene=name, preset=preset, mesh_instances=emitted, entities=len(entities),
                    triangles=triangles, mesh_counts=dict(counts),
                    districts={d: dict(mesh_instances=sum(c.values()), triangles=sum(c[n]*assets[n]["triangles"] for n in c))
                               for d, c in by_district.items()},
                    unique_geometry_bytes=dict(vertices=unique_vertex, indices=unique_index, total=unique_vertex+unique_index),
                    t4_instance_and_args_bytes_per_frame=count*180,
                    t4_instance_and_args_bytes_three_frames=count*540,
                    triangles_submitted_with_predepth_four_shadows_forward=triangles*6,
                    fixed_geometry_pool_reserved_bytes=384*1024*1024,
                    scene_file_bytes=scene_path.stat().st_size,
                    scene_sha256=hashlib.sha256(scene_path.read_bytes()).hexdigest(),
                    camera_sha256=hashlib.sha256(sidecar_path.read_bytes()).hexdigest(),
                    camera_targets=[c["Name"] for c in cameras],
                    geometry_count_basis="Bundled Assimp 4.x import with Cerberus flags; source SHA256 checked",
                    runtime_validated=False)
    scene_path.with_suffix(".manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8", newline="\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=REPO)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--preset", choices=["Low", "Medium", "High", "Extreme", "all"], default="Low")
    parser.add_argument("--instances", type=int, help="Override one preset's target; range 20..100000")
    parser.add_argument("--allow-large", action="store_true", help="Allow generation above 10000; does not launch engine")
    args = parser.parse_args()
    presets = json.loads((HERE / "presets.json").read_text())
    selected = presets if args.preset == "all" else {args.preset: presets[args.preset]}
    if args.instances is not None:
        if args.preset == "all":
            parser.error("--instances requires a single preset")
        selected[args.preset] = args.instances
    if any(n < 20 or n > 100000 for n in selected.values()):
        parser.error("Instance count must be 20..100000")
    if max(selected.values()) > 10000 and not args.allow_large:
        parser.error("High/Extreme are experimental; use --allow-large to generate without launching")
    output = args.output or args.repo / "Cerberus-Editor/Resources/Scenes/GPUStressTest"
    for preset, count in selected.items():
        result = generate(args.repo, output, preset, count)
        print(f"{result['scene']}: {count:,} instances; {result['triangles']:,} triangles; {result['scene_file_bytes']:,} scene bytes")


if __name__ == "__main__":
    main()
