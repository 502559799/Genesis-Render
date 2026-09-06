#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_fbx_test_asset.py - generate valid Kaydara FBX *binary* test assets for
the Genesis-Render native FBX importer (src/genesis/resource/fbx_importer.cpp).

Only the Python standard library + zlib are used (no third-party deps).

Outputs (defaults, overridable with --out / --sphere-out):
    assets/models/genesis_test_cube.fbx
        Unit cube (side 1, centered at origin): 8 control points, 6 quad
        polygons -> 12 triangles / 36 indices after fan triangulation,
        ByPolygonVertex normals + UVs (0..1 per face) + 4-component tangents,
        one Phong material (DiffuseColor 0.8/0.2/0.2, Shininess 32),
        one Model (identity Lcl TRS), full Definitions/Objects/Connections.
    assets/models/genesis_test_sphere.fbx
        UV sphere (16 segments x 12 rings, radius 0.5): mixed triangle
        (pole bands) + quad polygons -> exercises N-gon/triangle fan paths.

Both files use FBX version 7400 with 13-byte node headers (32-bit offsets),
which is the most widely compatible binary layout.  Array properties are
written with BOTH Encoding=0 (raw) and Encoding=1 (zlib deflate) across the
two assets so the importer's two decode paths are both covered.

---------------------------------------------------------------------------
FBX binary layout implemented here (all little endian)
---------------------------------------------------------------------------
File header (27 bytes):
    0..20   "Kaydara FBX Binary  \\x00"   (magic: two trailing spaces + NUL)
    21..22  0x1A 0x00                     (constant seen in every SDK file)
    23..26  uint32 version = 7400

Node record (version < 7500 -> 13-byte header; >= 7500 would use 64-bit
EndOffset/NumProperties/PropertyListLen and a 25-byte header):
    uint32  EndOffset        ABSOLUTE file offset of the end of this node,
                             including the NULL record that terminates its
                             nested child list (when it has children)
    uint32  NumProperties
    uint32  PropertyListLen  byte length of the property list (after Name)
    uint8   NameLen
    bytes   Name             (no NUL terminator)
    bytes   PropertyList     (NumProperties encoded properties)
    ...     nested child node records (optional)
    13 zero bytes            NULL record terminating the nested child list
                             (only present when the node has children)

NULL record (terminates any sibling node list, and the file's top level):
    13 zero bytes (EndOffset == 0 and NameLen == 0 is the sentinel).

Property encodings (tag byte + payload):
    'S'  u32 length + bytes           object names embed "\\x00\\x01ClassName"
    'L'  int64        'I' int32       'Y' int16       'C' uint8 (bool)
    'D'  float64      'F' float32     'R' u32 length + raw bytes
    'd'/'i'/'l'/'f'/'b'  typed arrays (float64/int32/int64/float32/uint8):
         u32 ArrayLength | u32 Encoding | u32 CompressedLength | payload
         Encoding 0 -> payload = raw elements, CompressedLength = raw size
         Encoding 1 -> payload = zlib.compress(raw), CompressedLength = its size

File footer (FBX >= 7.4 / SDK 2016+; structure verified against the fbxcel
v7400 writer - readers normally stop at the top-level NULL record and ignore
everything after it):
    13 zero bytes          top-level NULL record (closes the root node list)
    16 bytes               semirandom per-file id.  SDK-exported files use a
                           fixed high-nibble pattern; the canonical observed
                           value fa bc ab 09 d0 a8 8e 32 30 0c 10 06 fe f8
                           83 1c is written here.
    (-pos) & 0xF zeros     padding up to the next 16-byte file offset
    4 zero bytes           (unknown2, always zero)
    uint32 version echo    (7400 again)
    120 zero bytes
    16 bytes               trailer signature:
                           f8 5a 8c 6a de f5 d9 7e ec e9 0c e3 75 8f 29 0b
---------------------------------------------------------------------------
"""

import argparse
import math
import os
import struct
import sys
import zlib

FBX_VERSION = 7400

HEADER_MAGIC = b"Kaydara FBX Binary  \x00\x1a\x00" + struct.pack("<I", FBX_VERSION)

# Canonical SDK-observed footer file id (see module docstring).
FOOTER_FILE_ID = bytes([0xfa, 0xbc, 0xab, 0x09, 0xd0, 0xa8, 0x8e, 0x32,
                        0x30, 0x0c, 0x10, 0x06, 0xfe, 0xf8, 0x83, 0x1c])
# Constant 16-byte trailer signature at the very end of the file.
FOOTER_TRAILER = bytes([0xf8, 0x5a, 0x8c, 0x6a, 0xde, 0xf5, 0xd9, 0x7e,
                        0xec, 0xe9, 0x0c, 0xe3, 0x75, 0x8f, 0x29, 0x0b])

# ---------------------------------------------------------------------------
# Property encoding
# ---------------------------------------------------------------------------
# A property is a tuple: ('S', str) ('L', int) ('I', int) ('D', float)
#                        ('F', float) ('C', 0|1) ('Y', int) ('R', bytes)
#                        ('d', [floats], encoding) ('i', [ints], encoding)
#                        ('l', [ints], encoding) ('f', [floats], encoding)
#                        ('b', [ints], encoding)
_ARRAY_FMT = {"d": "<d", "i": "<i", "l": "<q", "f": "<f", "b": "<B"}


def encode_prop(prop):
    tag = prop[0]
    if tag == "S":
        raw = prop[1].encode("utf-8") if isinstance(prop[1], str) else bytes(prop[1])
        return b"S" + struct.pack("<I", len(raw)) + raw
    if tag == "L":
        return b"L" + struct.pack("<q", int(prop[1]))
    if tag == "I":
        return b"I" + struct.pack("<i", int(prop[1]))
    if tag == "D":
        return b"D" + struct.pack("<d", float(prop[1]))
    if tag == "F":
        return b"F" + struct.pack("<f", float(prop[1]))
    if tag == "C":
        return b"C" + struct.pack("<B", 1 if prop[1] else 0)
    if tag == "Y":
        return b"Y" + struct.pack("<h", int(prop[1]))
    if tag == "R":
        raw = bytes(prop[1])
        return b"R" + struct.pack("<I", len(raw)) + raw
    if tag in _ARRAY_FMT:
        values = prop[1]
        encoding = int(prop[2]) if len(prop) > 2 else 0
        fmt = _ARRAY_FMT[tag]
        raw = b"".join(struct.pack(fmt, v) for v in values)
        if encoding == 1:
            payload = zlib.compress(raw, 9)
        else:
            encoding = 0
            payload = raw  # Encoding 0: CompressedLength == raw byte length
        return (tag.encode("ascii")
                + struct.pack("<III", len(values), encoding, len(payload))
                + payload)
    raise ValueError("unknown property tag %r" % (tag,))


# ---------------------------------------------------------------------------
# Node model + binary writer
# ---------------------------------------------------------------------------
class Node(object):
    __slots__ = ("name", "props", "children")

    def __init__(self, name, props=None, children=None):
        self.name = name
        self.props = list(props or [])
        self.children = list(children or [])


class FbxBinaryWriter(object):
    """Serializes Node trees using the 13-byte (32-bit offset) node header."""

    def __init__(self, version=FBX_VERSION):
        assert version < 7500, "this writer emits 32-bit-offset node headers"
        self.version = version
        self.buf = bytearray()
        self.buf += HEADER_MAGIC

    def write_node(self, node):
        start = len(self.buf)
        # 13-byte header placeholder: EndOffset(4) NumProps(4) PropListLen(4)
        # NameLen(1).  NameLen lives at start+12 and is patched below by
        # pack_into - the name bytes must start immediately at start+13.
        self.buf += b"\x00" * 13
        name_bytes = node.name.encode("ascii")
        assert len(name_bytes) < 256
        self.buf += name_bytes
        prop_start = len(self.buf)
        for prop in node.props:
            self.buf += encode_prop(prop)
        prop_len = len(self.buf) - prop_start
        for child in node.children:
            self.write_node(child)
        if node.children:
            # NULL record that terminates this node's nested child list.
            # It lies INSIDE EndOffset.
            self.buf += b"\x00" * 13
        end = len(self.buf)
        assert end < 0xFFFFFFFF, "file too large for 32-bit EndOffset"
        struct.pack_into("<IIIB", self.buf, start,
                         end, len(node.props), prop_len, len(name_bytes))

    def finish(self):
        # Top-level NULL record closing the root node list.
        self.buf += b"\x00" * 13
        # --- footer (see module docstring for the full breakdown) ---
        self.buf += FOOTER_FILE_ID                              # 16-byte file id
        self.buf += b"\x00" * ((-len(self.buf)) & 0x0F)         # pad to 16-byte offset
        self.buf += b"\x00" * 4                                 # unknown2 (zeros)
        self.buf += struct.pack("<I", self.version)             # version echo
        self.buf += b"\x00" * 120                               # 120 zero bytes
        self.buf += FOOTER_TRAILER                              # 16-byte trailer magic
        return bytes(self.buf)


# ---------------------------------------------------------------------------
# Scene building blocks
# ---------------------------------------------------------------------------
def p70(name, type_name, type_class, flags, *values):
    """A Properties70 'P:' record: 4 header strings + value properties."""
    return Node("P", [("S", name), ("S", type_name), ("S", type_class), ("S", flags)]
                + list(values))


def vec3(name, type_name, type_class, flags, v):
    return p70(name, type_name, type_class, flags, ("D", v[0]), ("D", v[1]), ("D", v[2]))


def make_global_settings():
    # Engine-compatible defaults: +Y up / +Z front-axis / X coord / meters.
    return Node("GlobalSettings", [], [
        Node("Version", [("I", 1000)]),
        Node("Properties70", [], [
            p70("UpAxis", "int", "Integer", "", ("I", 1)),
            p70("UpAxisSign", "int", "Integer", "", ("I", 1)),
            p70("FrontAxis", "int", "Integer", "", ("I", 2)),
            p70("FrontAxisSign", "int", "Integer", "", ("I", 1)),
            p70("CoordAxis", "int", "Integer", "", ("I", 0)),
            p70("CoordAxisSign", "int", "Integer", "", ("I", 1)),
            p70("CoordAxisZSign", "int", "Integer", "", ("I", 1)),
            p70("UnitScaleFactor", "double", "Number", "", ("D", 1.0)),
            p70("OriginalUnitScaleFactor", "double", "Number", "", ("D", 1.0)),
        ]),
    ])


def make_definitions(counts):
    """counts: list of (ObjectType name, count)."""
    children = [Node("Version", [("I", 100)]),
                Node("Count", [("I", sum(c for _, c in counts))])]
    for type_name, count in counts:
        children.append(Node("ObjectType", [("S", type_name)],
                             [Node("Count", [("I", count)])]))
    return Node("Definitions", [], children)


def make_material(mat_id, name, diffuse, ambient, specular, emissive,
                  shininess, reflection_factor=0.0):
    return Node("Material",
                [("L", mat_id), ("S", "Material::%s\x00\x01Material" % name), ("S", "")],
                [
                    Node("Version", [("I", 102)]),
                    Node("ShadingModel", [("S", "Phong")]),
                    Node("MultiLayer", [("C", 0)]),
                    Node("Properties70", [], [
                        p70("ShadingModel", "KString", "", "", ("S", "Phong")),
                        p70("MultiLayer", "bool", "", "", ("C", 0)),
                        vec3("AmbientColor", "Color", "", "A", ambient),
                        vec3("DiffuseColor", "Color", "", "A", diffuse),
                        vec3("SpecularColor", "Color", "", "A", specular),
                        vec3("EmissiveColor", "Color", "", "A", emissive),
                        p70("Shininess", "Number", "", "A", ("D", shininess)),
                        p70("ShininessExponent", "Number", "", "A", ("D", shininess)),
                        p70("ReflectionFactor", "Number", "", "A", ("D", reflection_factor)),
                    ]),
                ])


def make_model(model_id, name, translation=(0.0, 0.0, 0.0),
               rotation=(0.0, 0.0, 0.0), scaling=(1.0, 1.0, 1.0),
               pre_rotation=(0.0, 0.0, 0.0)):
    return Node("Model",
                [("L", model_id), ("S", "Model::%s\x00\x01Model" % name), ("S", "Mesh")],
                [
                    Node("Version", [("I", 232)]),
                    Node("Properties70", [], [
                        p70("RotationActive", "bool", "", "", ("C", 1)),
                        p70("InheritType", "enum", "", "", ("I", 1)),
                        p70("RotationOrder", "enum", "", "", ("I", 0)),  # eEulerXYZ
                        vec3("PreRotation", "Vector3D", "Vector", "", pre_rotation),
                        vec3("Lcl Translation", "Lcl Type", "Lcl Translation", "", translation),
                        vec3("Lcl Rotation", "Lcl Type", "Lcl Rotation", "", rotation),
                        vec3("Lcl Scaling", "Lcl Type", "Lcl Scaling", "", scaling),
                    ]),
                    Node("Shading", [("C", 1)]),
                    Node("Culling", [("S", "CullingOff")]),
                ])


def make_geometry(geom_id, name, verts, pri, normals, uvs,
                  tangents=None, vertex_count_node=False,
                  verts_enc=0, pri_enc=0, normals_enc=0, uvs_enc=0, tangents_enc=0):
    children = [
        Node("Vertices", [("d", verts, verts_enc)]),
        Node("PolygonVertexIndex", [("i", pri, pri_enc)]),
    ]
    if vertex_count_node:
        children.append(Node("VertexCount", [("I", len(verts) // 3)]))
    children += [
        Node("GeometryVersion", [("I", 124)]),
        Node("LayerElementNormal", [("I", 0)], [
            Node("Version", [("I", 102)]),
            Node("Name", [("S", "")]),
            Node("MappingInformationType", [("S", "ByPolygonVertex")]),
            Node("ReferenceInformationType", [("S", "Direct")]),
            Node("Normals", [("d", normals, normals_enc)]),
        ]),
        Node("LayerElementUV", [("I", 0)], [
            Node("Version", [("I", 101)]),
            Node("Name", [("S", "UVMap")]),
            Node("MappingInformationType", [("S", "ByPolygonVertex")]),
            Node("ReferenceInformationType", [("S", "Direct")]),
            Node("UV", [("d", uvs, uvs_enc)]),
        ]),
        Node("LayerElementMaterial", [("I", 0)], [
            Node("Version", [("I", 101)]),
            Node("Name", [("S", "")]),
            Node("MappingInformationType", [("S", "AllSame")]),
            Node("ReferenceInformationType", [("S", "IndexToDirect")]),
            Node("Materials", [("i", [0], 0)]),
        ]),
    ]
    if tangents is not None:
        children.append(Node("LayerElementTangent", [("I", 0)], [
            Node("Version", [("I", 102)]),
            Node("Name", [("S", "")]),
            Node("MappingInformationType", [("S", "ByPolygonVertex")]),
            Node("ReferenceInformationType", [("S", "Direct")]),
            Node("Tangents", [("d", tangents, tangents_enc)]),
        ]))
    return Node("Geometry",
                [("L", geom_id), ("S", "Geometry::%s\x00\x01Geometry" % name), ("S", "Mesh")],
                children)


def oo(src_id, dst_id):
    return Node("C", [("S", "OO"), ("L", src_id), ("L", dst_id)])


# ---------------------------------------------------------------------------
# Cube scene
# ---------------------------------------------------------------------------
def build_cube_scene():
    geom_id, model_id, mat_id = 5550001, 5550002, 5550003

    # 8 control points of a unit cube centered at the origin.
    corners = [
        (-0.5, -0.5, -0.5),  # 0
        (-0.5,  0.5, -0.5),  # 1
        ( 0.5,  0.5, -0.5),  # 2
        ( 0.5, -0.5, -0.5),  # 3
        (-0.5, -0.5,  0.5),  # 4
        (-0.5,  0.5,  0.5),  # 5
        ( 0.5,  0.5,  0.5),  # 6
        ( 0.5, -0.5,  0.5),  # 7
    ]
    verts = [c for corner in corners for c in corner]

    # Six quads wound CCW seen from outside (cross product of the first two
    # edges points along the outward face normal).  The last corner of each
    # polygon is stored bitwise complemented (~idx == -idx-1).
    faces = [
        ((4, 7, 6, 5), ( 0.0,  0.0,  1.0), (1.0, 0.0, 0.0)),   # +Z, U along +X
        ((0, 1, 2, 3), ( 0.0,  0.0, -1.0), (0.0, 1.0, 0.0)),   # -Z, U along +Y
        ((3, 2, 6, 7), ( 1.0,  0.0,  0.0), (0.0, 1.0, 0.0)),   # +X, U along +Y
        ((0, 4, 5, 1), (-1.0,  0.0,  0.0), (0.0, 0.0, 1.0)),   # -X, U along +Z
        ((1, 5, 6, 2), ( 0.0,  1.0,  0.0), (0.0, 0.0, 1.0)),   # +Y, U along +Z
        ((0, 3, 7, 4), ( 0.0, -1.0,  0.0), (1.0, 0.0, 0.0)),   # -Y, U along +X
    ]
    quad_uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]

    pri, normals, uvs, tangents = [], [], [], []
    for (idxs, nrm, tan) in faces:
        for k, vi in enumerate(idxs):
            pri.append(vi if k < len(idxs) - 1 else ~vi)
            normals += list(nrm)
            uvs += list(quad_uvs[k])
            # 4-component tangent: xyz direction + handedness w = -1
            # (FBX/Blender convention: bitangent = cross(n, t) * w).
            tangents += [tan[0], tan[1], tan[2], -1.0]

    geometry = make_geometry(
        geom_id, "GenesisCube", verts, pri, normals, uvs, tangents=tangents,
        verts_enc=0,      # raw float64 positions
        pri_enc=1,        # zlib-compressed int32 indices  (exercises Encoding 1)
        normals_enc=1,    # zlib-compressed normals        (exercises Encoding 1)
        uvs_enc=0,        # raw UVs
        tangents_enc=0,
    )
    material = make_material(mat_id, "GenesisRed",
                             diffuse=(0.8, 0.2, 0.2),
                             ambient=(0.2, 0.2, 0.2),
                             specular=(0.1, 0.1, 0.1),
                             emissive=(0.0, 0.0, 0.0),
                             shininess=32.0)
    model = make_model(model_id, "GenesisCube")

    doc = [
        Node("FBXHeaderExtension", [], [
            Node("FBXHeaderVersion", [("I", 1003)]),
            Node("FBXVersion", [("I", FBX_VERSION)]),
            Node("CreationTime", [("S", "2026-09-06 12:00:00:000")]),
            Node("Creator", [("S", "Genesis-Render gen_fbx_test_asset.py")]),
        ]),
        make_global_settings(),
        make_definitions([("GlobalSettings", 1), ("Geometry", 1),
                          ("Model", 1), ("Material", 1)]),
        Node("Objects", [], [geometry, material, model]),
        Node("Connections", [], [oo(geom_id, model_id), oo(mat_id, model_id)]),
    ]
    stats = {"verts": len(verts) // 3, "polys": len(faces), "corners": len(pri)}
    return doc, stats


# ---------------------------------------------------------------------------
# Sphere scene (mixed triangles + quads)
# ---------------------------------------------------------------------------
def build_sphere_scene(segs=16, rings=12, radius=0.5):
    geom_id, model_id, mat_id = 5550101, 5550102, 5550103

    # (rings+1) * (segs+1) grid: seam and poles are duplicated vertices with
    # distinct UVs, exactly like a classic UV sphere.
    pts = []
    for j in range(rings + 1):
        phi = math.pi * j / rings
        for i in range(segs + 1):
            theta = 2.0 * math.pi * i / segs
            pts.append((radius * math.sin(phi) * math.cos(theta),
                        radius * math.cos(phi),
                        radius * math.sin(phi) * math.sin(theta)))
    verts = [c for p in pts for c in p]

    def vid(i, j):
        return j * (segs + 1) + i

    pri, normals, uvs = [], [], []
    n_polys = 0
    for j in range(rings):
        for i in range(segs):
            a, b = vid(i, j), vid(i + 1, j)
            c, d = vid(i, j + 1), vid(i + 1, j + 1)
            if j == 0:
                corners = (a, d, c)        # north pole band: triangles
            elif j == rings - 1:
                corners = (a, b, d)        # south pole band: triangles
            else:
                corners = (a, b, d, c)     # middle bands: quads
            n_polys += 1
            for k, vi in enumerate(corners):
                pri.append(vi if k < len(corners) - 1 else ~vi)
                px, py, pz = pts[vi]
                normals.append(px / radius)
                normals.append(py / radius)
                normals.append(pz / radius)
                uvs.append((vi % (segs + 1)) / segs)
                uvs.append((vi // (segs + 1)) / rings)

    geometry = make_geometry(
        geom_id, "GenesisSphere", verts, pri, normals, uvs,
        vertex_count_node=True,
        verts_enc=1,      # zlib-compressed positions     (exercises Encoding 1)
        pri_enc=0,        # raw indices
        normals_enc=0,    # raw normals
        uvs_enc=1,        # zlib-compressed UVs           (exercises Encoding 1)
    )
    material = make_material(mat_id, "GenesisBlue",
                             diffuse=(0.2, 0.4, 0.8),
                             ambient=(0.1, 0.1, 0.1),
                             specular=(0.2, 0.2, 0.2),
                             emissive=(0.0, 0.0, 0.0),
                             shininess=64.0)
    model = make_model(model_id, "GenesisSphere")

    doc = [
        Node("FBXHeaderExtension", [], [
            Node("FBXHeaderVersion", [("I", 1003)]),
            Node("FBXVersion", [("I", FBX_VERSION)]),
            Node("CreationTime", [("S", "2026-09-06 12:00:00:000")]),
            Node("Creator", [("S", "Genesis-Render gen_fbx_test_asset.py")]),
        ]),
        make_global_settings(),
        make_definitions([("GlobalSettings", 1), ("Geometry", 1),
                          ("Model", 1), ("Material", 1)]),
        Node("Objects", [], [geometry, material, model]),
        Node("Connections", [], [oo(geom_id, model_id), oo(mat_id, model_id)]),
    ]
    stats = {"verts": len(verts) // 3, "polys": n_polys, "corners": len(pri)}
    return doc, stats


# ---------------------------------------------------------------------------
# File emission
# ---------------------------------------------------------------------------
def write_fbx(path, doc):
    writer = FbxBinaryWriter(FBX_VERSION)
    for node in doc:
        writer.write_node(node)
    payload = writer.finish()
    directory = os.path.dirname(os.path.abspath(path))
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, "wb") as f:
        f.write(payload)
    return len(payload)


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_cube = os.path.join(repo_root, "assets", "models", "genesis_test_cube.fbx")
    default_sphere = os.path.join(repo_root, "assets", "models", "genesis_test_sphere.fbx")

    ap = argparse.ArgumentParser(
        description="Generate FBX binary test assets for the Genesis-Render FBX importer.")
    ap.add_argument("--out", default=default_cube,
                    help="output path of the cube asset (default: %(default)s)")
    ap.add_argument("--sphere-out", default=default_sphere,
                    help="output path of the sphere asset (default: %(default)s)")
    args = ap.parse_args()

    cube_doc, cube_stats = build_cube_scene()
    sphere_doc, sphere_stats = build_sphere_scene()

    cube_size = write_fbx(args.out, cube_doc)
    sphere_size = write_fbx(args.sphere_out, sphere_doc)

    print("wrote %s" % args.out)
    print("  %d bytes | %d control points | %d quad polygons | %d polygon corners"
          % (cube_size, cube_stats["verts"], cube_stats["polys"], cube_stats["corners"]))
    print("  -> importer expectation: 24 deduped vertices, 36 indices (12 triangles)")
    print("wrote %s" % args.sphere_out)
    print("  %d bytes | %d control points | %d polygons (32 tris + 160 quads) | %d corners"
          % (sphere_size, sphere_stats["verts"], sphere_stats["polys"], sphere_stats["corners"]))
    print("  -> importer expectation: 219 deduped vertices (2 seam-pole grid points are")
    print("     unreferenced), 1056 indices (32*1 + 160*2 triangles)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
