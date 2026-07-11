namespace Prani.Engine.Scene;

/// <summary>
/// The document: a flat list of nodes plus selection and dirty state.
/// Render-thread owned. Named Scene3D to avoid clashing with the namespace.
/// </summary>
public sealed class Scene3D
{
    private int _nextId = 1;

    public List<SceneNode> Nodes { get; } = new();
    public int SelectedId { get; set; } = -1;
    /// <summary>Path of the .prani file this scene was loaded from / saved to. Empty = untitled.</summary>
    public string FilePath { get; set; } = string.Empty;
    public bool Dirty { get; set; }

    public SceneNode? Find(int id) => Nodes.Find(n => n.Id == id);
    public SceneNode? Selected => Find(SelectedId);

    public SceneNode AddNode(string name, string sourcePath)
    {
        var node = new SceneNode { Id = _nextId++, Name = name, SourcePath = sourcePath };
        Nodes.Add(node);
        Dirty = true;
        return node;
    }

    public void RemoveNode(int id)
    {
        var node = Find(id);
        if (node is null) return;
        node.Model?.Unload();
        Nodes.Remove(node);
        if (SelectedId == id) SelectedId = -1;
        Dirty = true;
    }

    public void Clear()
    {
        foreach (var n in Nodes) n.Model?.Unload();
        Nodes.Clear();
        SelectedId = -1;
        FilePath = string.Empty;
        Dirty = false;
        _nextId = 1;
    }
}
