using System.Numerics;
using Assimp;
using Raylib_cs;

namespace Prani.Engine.Import;

/// <summary>
/// FBX (also OBJ/glTF/DAE — anything Assimp reads) → raylib meshes.
///
/// Pipeline:
///   1. Assimp import with Triangulate + PreTransformVertices (bakes the node hierarchy —
///      right for static preview; skeletal import is a later milestone, see docs/09_Roadmap.md).
///   2. Each Assimp mesh becomes one raylib Mesh. raylib indices are 16-bit, so meshes
///      with >65535 vertices are de-indexed (one vertex per triangle corner) instead.
///   3. Material diffuse color is carried over; textures are a roadmap item.
///
/// MUST be called on the render thread — UploadMesh talks to the GL context.
/// </summary>
public static class FbxImporter
{
    private static readonly PostProcessSteps Steps =
        PostProcessSteps.Triangulate |
        PostProcessSteps.GenerateSmoothNormals |
        PostProcessSteps.JoinIdenticalVertices |
        PostProcessSteps.PreTransformVertices |
        PostProcessSteps.ImproveCacheLocality;

    public static ImportedModel Import(string path)
    {
        using var ctx = new AssimpContext();
        Assimp.Scene scene = ctx.ImportFile(path, Steps)
            ?? throw new InvalidDataException($"Assimp failed to read '{path}'.");

        var model = new ImportedModel { SourcePath = path };
        var min = new Vector3(float.MaxValue);
        var max = new Vector3(float.MinValue);
        int triangles = 0;

        foreach (var srcMesh in scene.Meshes)
        {
            if (srcMesh.PrimitiveType != Assimp.PrimitiveType.Triangle || srcMesh.VertexCount == 0)
                continue;

            var slot = new ImportedModel.MeshSlot { Name = srcMesh.Name ?? "mesh" };
            slot.Mesh = srcMesh.VertexCount <= ushort.MaxValue
                ? BuildIndexedMesh(srcMesh)
                : BuildDeindexedMesh(srcMesh);

            slot.Material = Raylib.LoadMaterialDefault();
            slot.Albedo = ResolveDiffuse(scene, srcMesh);
            SetAlbedo(ref slot.Material, slot.Albedo);

            model.Slots.Add(slot);
            triangles += srcMesh.FaceCount;

            foreach (var v in srcMesh.Vertices)
            {
                min = Vector3.Min(min, new Vector3(v.X, v.Y, v.Z));
                max = Vector3.Max(max, new Vector3(v.X, v.Y, v.Z));
            }
        }

        if (model.Slots.Count == 0)
            throw new InvalidDataException($"'{path}' contains no triangle meshes.");

        model.Bounds = new BoundingBox(min, max);
        model.TriangleCount = triangles;
        Log.Info($"Imported '{Path.GetFileName(path)}': {model.Slots.Count} mesh(es), {triangles} tris.");
        return model;
    }

    private static Raylib_cs.Mesh BuildIndexedMesh(Assimp.Mesh src)
    {
        int vCount = src.VertexCount;
        int tCount = src.FaceCount;
        var mesh = new Raylib_cs.Mesh(vCount, tCount);
        mesh.AllocVertices();
        mesh.AllocNormals();
        mesh.AllocTexCoords();
        mesh.AllocIndices();

        Span<Vector3> verts = mesh.VerticesAs<Vector3>();
        Span<Vector3> normals = mesh.NormalsAs<Vector3>();
        Span<Vector2> uvs = mesh.TexCoordsAs<Vector2>();
        Span<ushort> indices = mesh.IndicesAs<ushort>();

        bool hasUv = src.HasTextureCoords(0);
        for (int i = 0; i < vCount; i++)
        {
            var p = src.Vertices[i];
            var n = src.Normals[i];
            verts[i] = new Vector3(p.X, p.Y, p.Z);
            normals[i] = new Vector3(n.X, n.Y, n.Z);
            uvs[i] = hasUv
                ? new Vector2(src.TextureCoordinateChannels[0][i].X, 1.0f - src.TextureCoordinateChannels[0][i].Y)
                : Vector2.Zero;
        }

        int k = 0;
        foreach (var face in src.Faces)
        {
            indices[k++] = (ushort)face.Indices[0];
            indices[k++] = (ushort)face.Indices[1];
            indices[k++] = (ushort)face.Indices[2];
        }

        Raylib.UploadMesh(ref mesh, false);
        return mesh;
    }

    /// <summary>>65535 vertices: expand to one vertex per corner so no index buffer is needed.</summary>
    private static Raylib_cs.Mesh BuildDeindexedMesh(Assimp.Mesh src)
    {
        int tCount = src.FaceCount;
        int vCount = tCount * 3;
        var mesh = new Raylib_cs.Mesh(vCount, tCount);
        mesh.AllocVertices();
        mesh.AllocNormals();
        mesh.AllocTexCoords();

        Span<Vector3> verts = mesh.VerticesAs<Vector3>();
        Span<Vector3> normals = mesh.NormalsAs<Vector3>();
        Span<Vector2> uvs = mesh.TexCoordsAs<Vector2>();

        bool hasUv = src.HasTextureCoords(0);
        int k = 0;
        foreach (var face in src.Faces)
        {
            for (int c = 0; c < 3; c++)
            {
                int i = face.Indices[c];
                var p = src.Vertices[i];
                var n = src.Normals[i];
                verts[k] = new Vector3(p.X, p.Y, p.Z);
                normals[k] = new Vector3(n.X, n.Y, n.Z);
                uvs[k] = hasUv
                    ? new Vector2(src.TextureCoordinateChannels[0][i].X, 1.0f - src.TextureCoordinateChannels[0][i].Y)
                    : Vector2.Zero;
                k++;
            }
        }

        Raylib.UploadMesh(ref mesh, false);
        return mesh;
    }

    private static Color ResolveDiffuse(Assimp.Scene scene, Assimp.Mesh mesh)
    {
        if (mesh.MaterialIndex >= 0 && mesh.MaterialIndex < scene.MaterialCount)
        {
            var mat = scene.Materials[mesh.MaterialIndex];
            if (mat.HasColorDiffuse)
            {
                var c = mat.ColorDiffuse;
                return new Color(
                    (byte)Math.Clamp(c.R * 255.0f, 0, 255),
                    (byte)Math.Clamp(c.G * 255.0f, 0, 255),
                    (byte)Math.Clamp(c.B * 255.0f, 0, 255),
                    (byte)255);
            }
        }
        return new Color(200, 200, 205, 255);
    }

    private static unsafe void SetAlbedo(ref Raylib_cs.Material material, Color color)
    {
        material.Maps[(int)MaterialMapIndex.Albedo].Color = color;
    }
}
