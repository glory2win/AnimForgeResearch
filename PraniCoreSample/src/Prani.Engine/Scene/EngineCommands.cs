using System.Numerics;

namespace Prani.Engine.Scene;

/// <summary>
/// Everything the UI (Avalonia shell OR ImGui panels) wants changed in the scene travels
/// as one of these records through RenderHost.Enqueue. They are applied on the render
/// thread at the top of the frame — the only place scene state is ever mutated.
/// </summary>
public interface IEngineCommand { }

public sealed record NewSceneCommand : IEngineCommand;
public sealed record OpenSceneCommand(string Path) : IEngineCommand;
public sealed record SaveSceneCommand(string Path) : IEngineCommand;
public sealed record ImportModelCommand(string Path) : IEngineCommand;
public sealed record RemoveNodeCommand(int NodeId) : IEngineCommand;
public sealed record SelectNodeCommand(int NodeId) : IEngineCommand;
public sealed record SetNodeTransformCommand(int NodeId, Vector3 Position, Vector3 RotationDeg, Vector3 Scale) : IEngineCommand;
public sealed record SetNodeVisibleCommand(int NodeId, bool Visible) : IEngineCommand;
public sealed record ShutdownCommand : IEngineCommand;
