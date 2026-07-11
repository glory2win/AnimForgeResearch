# 06 — FBX import

## Pipeline

`FbxImporter` (Prani.Engine/Import) uses **AssimpNet** — so "FBX import" is really
"anything Assimp reads": FBX, OBJ, glTF/GLB, DAE, PLY, STL…

```
AssimpContext.ImportFile(path,
    Triangulate               // quads/ngons → triangles (raylib draws triangles)
  | GenerateSmoothNormals     // only where normals are missing
  | JoinIdenticalVertices     // rebuild index buffer, shrink vertex count
  | PreTransformVertices      // bake the whole node hierarchy into mesh space
  | ImproveCacheLocality)     // vertex-cache-friendly index order
        │
        ▼  per Assimp mesh
raylib Mesh (positions, normals, UV0, indices) → UploadMesh → GPU
        +  LoadMaterialDefault with the material's diffuse color
        ▼
ImportedModel { List<MeshSlot>, Bounds, TriangleCount }
```

An `ImportedModel` is a list of `(Mesh, Material, Color)` slots drawn with one shared
node transform — we skip raylib's `Model` struct because managing its unmanaged arrays
from C# is more code than looping `DrawMesh`.

**Must run on the render thread**: `UploadMesh` needs the GL context. That's why the
importer is invoked from `RenderHost.Apply`, never from the Avalonia side.

## The 16-bit index limit

raylib index buffers are `ushort` ⇒ max 65 535 vertices per indexed mesh. The importer
handles both cases:

* ≤ 65 535 → `BuildIndexedMesh` (shared vertices, small memory).
* > 65 535 → `BuildDeindexedMesh`: one vertex per triangle corner, no index buffer.
  ~3× vertex memory, but correct for arbitrarily large meshes.

Alternative for later: split big meshes into ≤64 k chunks to keep indexing.

## Units and axes — what to expect from FBX

| Source | Convention | Result in Prani (raylib = Y-up, meters-ish, right-handed) |
|---|---|---|
| Maya (default) | Y-up, **cm** | 100× too big → set node Scale 0.01 |
| 3ds Max / Blender FBX | Z-up, varies | Lying on its side → node Rotation X = −90 |
| glTF | Y-up, meters | Correct as-is (prefer it when you control the export) |

`PreTransformVertices` applies whatever unit/axis conversion the exporter wrote into the
FBX root node, which fixes many files automatically — but not all exporters write it.
The pragmatic path (and what this sample intends): fix per-node with the transform
properties, or add an import-options dialog later (scale factor + up-axis dropdown, then
bake into the vertices at import).

## UVs and winding

* Assimp UVs are bottom-left origin; the importer flips V (`1 − v`) for raylib's top-left.
* Winding is passed through as-is. If a mesh renders inside-out, its exporter flipped
  winding — toggle **Backface culling** in the Stats panel to confirm, then fix the export
  (or add `PostProcessSteps.FlipWindingOrder` for that asset).

## Extending the importer

* **Diffuse textures**: `material.HasTextureDiffuse` → resolve `TextureDiffuse.FilePath`
  relative to the model file → `Raylib.LoadTexture` → `SetMaterialTexture(ref mat,
  MaterialMapIndex.Albedo, tex)`. Cache by absolute path; unload with the model.
* **Skeletons** (the real goal): drop `PreTransformVertices`, read `mesh.Bones`
  (offset matrices + vertex weights) and `scene.Animations` (node channels), and either
  skin on CPU into a dynamic mesh (`UpdateMeshBuffer`) or write a GPU-skinning shader.
  Plan: import → `AnimForge.Core` pose buffers → sampling/blending in core math → skinned
  draw in the engine. See [09_Roadmap.md](09_Roadmap.md).
