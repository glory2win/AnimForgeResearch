using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Prani.Engine.Scene;

/// <summary>
/// .prani scene files: plain JSON with asset paths + transforms. Models are re-imported
/// on load, so scene files stay tiny and diff-able (good for git and for users).
/// </summary>
public static class SceneSerializer
{
    private sealed record NodeDto(string Name, string SourcePath,
        float[] Position, float[] RotationDeg, float[] Scale, bool Visible);

    private sealed record SceneDto(string App, string Version, List<NodeDto> Nodes);

    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    public static void Save(Scene3D scene, string path)
    {
        var dto = new SceneDto("Prani", AnimForge.Core.All.Version,
            scene.Nodes.Select(n => new NodeDto(
                n.Name, n.SourcePath,
                new[] { n.Position.X, n.Position.Y, n.Position.Z },
                new[] { n.RotationDeg.X, n.RotationDeg.Y, n.RotationDeg.Z },
                new[] { n.Scale.X, n.Scale.Y, n.Scale.Z },
                n.Visible)).ToList());

        File.WriteAllText(path, JsonSerializer.Serialize(dto, Options));
        scene.FilePath = path;
        scene.Dirty = false;
    }

    /// <summary>
    /// Reads the file and re-creates nodes. Model loading is delegated so the caller
    /// (RenderHost) can run the importer and report per-asset failures.
    /// </summary>
    public static void Load(Scene3D scene, string path, Action<SceneNode> loadModel)
    {
        var dto = JsonSerializer.Deserialize<SceneDto>(File.ReadAllText(path), Options)
                  ?? throw new InvalidDataException($"Not a valid .prani scene: {path}");

        scene.Clear();
        foreach (var n in dto.Nodes)
        {
            var node = scene.AddNode(n.Name, n.SourcePath);
            node.Position = new Vector3(n.Position[0], n.Position[1], n.Position[2]);
            node.RotationDeg = new Vector3(n.RotationDeg[0], n.RotationDeg[1], n.RotationDeg[2]);
            node.Scale = new Vector3(n.Scale[0], n.Scale[1], n.Scale[2]);
            node.Visible = n.Visible;
            if (!string.IsNullOrEmpty(node.SourcePath))
                loadModel(node);
        }
        scene.FilePath = path;
        scene.Dirty = false;
    }
}
