using System.Numerics;

namespace Prani.Engine.Scene;

/// <summary>
/// Immutable copy of the scene state handed to the UI thread after every mutation.
/// The Avalonia shell binds to this; it never touches live SceneNode objects.
/// </summary>
public sealed record SceneSnapshot(
    string FilePath,
    bool Dirty,
    int SelectedId,
    IReadOnlyList<SceneSnapshot.NodeInfo> Nodes)
{
    public sealed record NodeInfo(
        int Id,
        string Name,
        string SourcePath,
        Vector3 Position,
        Vector3 RotationDeg,
        Vector3 Scale,
        bool Visible,
        int MeshCount,
        int TriangleCount);

    public static SceneSnapshot Capture(Scene3D scene)
    {
        var nodes = new List<NodeInfo>(scene.Nodes.Count);
        foreach (var n in scene.Nodes)
        {
            nodes.Add(new NodeInfo(
                n.Id, n.Name, n.SourcePath,
                n.Position, n.RotationDeg, n.Scale, n.Visible,
                n.Model?.Slots.Count ?? 0,
                n.Model?.TriangleCount ?? 0));
        }
        return new SceneSnapshot(scene.FilePath, scene.Dirty, scene.SelectedId, nodes);
    }
}
