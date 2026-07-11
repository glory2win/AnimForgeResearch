using System.Numerics;
using Prani.Engine.Import;

namespace Prani.Engine.Scene;

/// <summary>
/// One entry in the scene: an imported model instance with a TRS transform.
/// Owned and mutated ONLY by the render thread; the UI sees SceneSnapshot copies.
/// </summary>
public sealed class SceneNode
{
    public int Id { get; init; }
    public string Name { get; set; } = "Node";
    /// <summary>Absolute path of the source asset (.fbx/.obj/.gltf); empty for primitives.</summary>
    public string SourcePath { get; set; } = string.Empty;

    public Vector3 Position = Vector3.Zero;
    /// <summary>Euler degrees, applied Z*Y*X (roll, yaw, pitch order below).</summary>
    public Vector3 RotationDeg = Vector3.Zero;
    public Vector3 Scale = Vector3.One;
    public bool Visible = true;

    /// <summary>GPU-side data. Null until the importer has run (or when load failed).</summary>
    public ImportedModel? Model;

    public Matrix4x4 WorldMatrix()
    {
        var r = Matrix4x4.CreateFromYawPitchRoll(
            RotationDeg.Y * MathF.PI / 180.0f,
            RotationDeg.X * MathF.PI / 180.0f,
            RotationDeg.Z * MathF.PI / 180.0f);
        return Matrix4x4.CreateScale(Scale) * r * Matrix4x4.CreateTranslation(Position);
    }
}
