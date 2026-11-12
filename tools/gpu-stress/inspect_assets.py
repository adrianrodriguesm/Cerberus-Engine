"""Read geometry through the existing Assimp DLL, using Cerberus' import flags.

No Python packages required. Writes source fingerprints and imported bounds/counts.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path


class Vec(C.Structure):
    _fields_ = [(n, C.c_float) for n in ("x", "y", "z")]


class Mesh(C.Structure):
    _fields_ = [("primitive", C.c_uint), ("vertices", C.c_uint), ("faces", C.c_uint),
                ("positions", C.POINTER(Vec))]


class String(C.Structure):
    # The bundled vc141 DLL is Assimp 4.x (size_t length), not the newer
    # source headers in vendor/assimp. Check its version before interpreting ABI.
    _fields_ = [("length", C.c_size_t), ("data", C.c_char * 1024)]


class Node(C.Structure):
    pass


Node._fields_ = [("name", String), ("matrix", C.c_float * 16), ("parent", C.POINTER(Node)),
                 ("num_children", C.c_uint), ("children", C.POINTER(C.POINTER(Node))),
                 ("num_meshes", C.c_uint), ("meshes", C.POINTER(C.c_uint))]


class Scene(C.Structure):
    _fields_ = [("flags", C.c_uint), ("root", C.POINTER(Node)),
                ("num_meshes", C.c_uint), ("meshes", C.POINTER(C.POINTER(Mesh)))]


def multiply(a, b):
    return [sum(a[r*4+k]*b[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("assets.json"))
    args = parser.parse_args()
    dll = C.CDLL(str(args.dll.resolve()))
    dll.aiGetVersionMajor.restype = C.c_uint
    if dll.aiGetVersionMajor() != 4:
        raise RuntimeError("This inspector supports the bundled Assimp 4.x DLL ABI only")
    dll.aiImportFile.argtypes = [C.c_char_p, C.c_uint]
    dll.aiImportFile.restype = C.POINTER(Scene)
    dll.aiReleaseImport.argtypes = [C.POINTER(Scene)]
    dll.aiReleaseImport.restype = None
    # CalcTangentSpace, Triangulate, SortByPType, GenNormals, GenUVCoords,
    # OptimizeMeshes, JoinIdenticalVertices, ValidateDataStructure.
    flags = 0x1 | 0x8 | 0x8000 | 0x20 | 0x40000 | 0x200000 | 0x2 | 0x400
    inventory = {}
    for name in ("Cube", "Cylinder", "Torus", "Cone"):
        relative = f"Cerberus-Editor/Resources/Meshes/Source/Default/{name}.fbx"
        path = args.repo / relative
        scene_ptr = dll.aiImportFile(str(path.resolve()).encode(), flags)
        if not scene_ptr:
            raise RuntimeError(f"Assimp failed: {path}")
        try:
            scene = scene_ptr.contents
            assert scene.num_meshes == 1, "Generator accounting expects one submesh per default asset"
            mesh = scene.meshes[0].contents
            assert mesh.primitive == 4, "Expected only triangle primitives"
            low, high = [float("inf")]*3, [float("-inf")]*3
            nodes = 0

            def visit(ptr, parent):
                nonlocal nodes
                node = ptr.contents
                matrix = multiply(parent, list(node.matrix))
                for j in range(node.num_meshes):
                    assert node.meshes[j] == 0
                    nodes += 1
                    for i in range(mesh.vertices):
                        p = mesh.positions[i]
                        v = [p.x, p.y, p.z, 1]
                        for axis in range(3):
                            x = sum(matrix[axis*4+k]*v[k] for k in range(4))
                            low[axis], high[axis] = min(low[axis], x), max(high[axis], x)
                for i in range(node.num_children):
                    visit(node.children[i], matrix)

            visit(scene.root, [1 if i//4 == i%4 else 0 for i in range(16)])
            assert nodes == 1
            inventory[name] = dict(source=relative, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                                   vertices=mesh.vertices, triangles=mesh.faces, submeshes=scene.num_meshes,
                                   imported_min=low, imported_max=high)
        finally:
            dll.aiReleaseImport(scene_ptr)
    args.output.write_text(json.dumps(inventory, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(inventory, indent=2))


if __name__ == "__main__":
    main()
