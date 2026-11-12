"""Independent accounting, references and determinism checks for generated assets."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import tempfile

from generate import REPO, generate, inputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=REPO)
    parser.add_argument("--scenes", type=Path)
    args = parser.parse_args()
    folder = args.scenes or args.repo / "Cerberus-Editor/Resources/Scenes/GPUStressTest"
    assets, handles = inputs(args.repo)
    lookup = {handles[name]: name for name in assets}
    paths = sorted(folder.glob("*.cscene"))
    assert paths, "No generated scenes found"
    for path in paths:
        scene = json.loads(path.read_text())
        manifest = json.loads(path.with_suffix(".manifest.json").read_text())
        cameras = json.loads(path.with_suffix(".benchmark.yaml").read_text())["Cameras"]
        entities = scene["Entities"]
        meshes = [e for e in entities if "MeshComponent" in e]
        counts = Counter(lookup[e["MeshComponent"]["AssetHandle"]] for e in meshes)
        assert len({e["Entity"] for e in entities}) == len(entities)
        assert len(meshes) == manifest["mesh_instances"]
        assert dict(counts) == manifest["mesh_counts"]
        assert sum(counts[n]*assets[n]["triangles"] for n in counts) == manifest["triangles"]
        assert sum("DirectionalLightComponent" in e for e in entities) == 1
        assert sum("SkyLightComponent" in e for e in entities) == 1
        assert scene["Renderer"] == "Forward"
        assert len(cameras) == 3 and len({c["Name"] for c in cameras}) == 3
        for entity in meshes:
            assert entity["MeshComponent"]["MaterialTable"]["0"] == handles["Material"]
            assert all(v > 0 for v in entity["TransformComponent"]["Scale"])
        # Use transformed conservative mesh AABBs to verify camera C has
        # substantial geometry outside its horizontal frustum. This is an
        # offline diagnostic, not runtime culling or a visibility oracle.
        c = cameras[2]
        yaw = math.radians(c["Yaw"])
        forward, right = (math.cos(yaw), math.sin(yaw)), (-math.sin(yaw), math.cos(yaw))
        tan_half_h = math.tan(math.radians(c["FOV"])/2)*16/9
        outside = 0
        for e in meshes:
            p = e["TransformComponent"]["Translation"]
            scale = e["TransformComponent"]["Scale"]
            asset = assets[lookup[e["MeshComponent"]["AssetHandle"]]]
            radius = .5*math.sqrt(sum(((hi-lo)*s)**2 for lo, hi, s in zip(asset["imported_min"], asset["imported_max"], scale)))
            dx, dz = p[0]-c["Position"][0], p[2]-c["Position"][2]
            depth, lateral = dx*forward[0]+dz*forward[1], abs(dx*right[0]+dz*right[1])
            outside += depth+radius < 0 or lateral > max(0, depth)*tan_half_h+radius*math.sqrt(1+tan_half_h**2)
        assert outside > len(meshes)*.2, "Expected substantial off-frustum geometry"
        with tempfile.TemporaryDirectory(prefix="cerberus-stress-") as temp:
            regenerated = generate(args.repo, Path(temp), manifest["preset"], len(meshes))
            assert regenerated["scene_sha256"] == hashlib.sha256(path.read_bytes()).hexdigest()
            assert regenerated["camera_sha256"] == manifest["camera_sha256"]
        print(f"{path.name}: references/counts/determinism OK; {manifest['triangles']:,} triangles; {outside:,} off-frustum candidates for C")


if __name__ == "__main__":
    main()
