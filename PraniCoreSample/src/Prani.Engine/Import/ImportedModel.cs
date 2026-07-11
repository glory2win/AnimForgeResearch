using Raylib_cs;

namespace Prani.Engine.Import;

/// <summary>
/// GPU-resident result of an import: one raylib Mesh+Material pair per source mesh.
/// We deliberately avoid raylib's Model struct (unmanaged arrays are awkward from C#);
/// drawing a list of DrawMesh calls with one shared transform is equivalent for our use.
/// </summary>
public sealed class ImportedModel : IDisposable
{
    public sealed class MeshSlot
    {
        public string Name = "mesh";
        public Mesh Mesh;
        public Material Material;
        public Color Albedo = Color.White;
    }

    public List<MeshSlot> Slots { get; } = new();
    public BoundingBox Bounds;
    public int TriangleCount;
    public string SourcePath = string.Empty;

    private bool _unloaded;

    public void Unload()
    {
        if (_unloaded) return;
        _unloaded = true;
        foreach (var slot in Slots)
        {
            Raylib.UnloadMesh(slot.Mesh);
            // Default materials share the default shader/texture — raylib handles that;
            // UnloadMaterial would try to unload the shared default shader, so skip it.
        }
        Slots.Clear();
    }

    public void Dispose() => Unload();
}
