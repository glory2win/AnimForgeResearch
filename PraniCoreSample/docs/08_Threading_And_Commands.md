# 08 — Threading & commands

## Two threads, one contract

| Thread | Owns | Never touches |
|---|---|---|
| Main (Avalonia UI) | Windows, dialogs, view models | raylib, ImGui, live `Scene3D` |
| `Prani.RenderThread` | GL context, scene, importer, ImGui | Avalonia objects |

GL contexts are thread-affine, and Avalonia's dispatcher is too — so **nothing is shared
mutable**. The entire cross-thread surface is three types:

```
UI ──► engine :  IEngineCommand  (records, enqueued on ConcurrentQueue)
engine ──► UI :  SceneSnapshot   (immutable copy after every mutation)
engine ──► UI :  LogEntry        (event from the shared Log sink)
```

## Command side

* `RenderHost.Enqueue` is the only entry point; safe from any thread.
* Commands drain at the **top of the frame** (`DrainCommands`), so a frame always renders
  a consistent scene — no mid-frame mutation.
* Each command is applied in one `switch` (`RenderHost.Apply`). Exceptions are caught per
  command and logged — a bad FBX never kills the loop.
* ImGui panels use the same queue even though they run on the render thread. Slight
  ceremony, big payoff: one mutation path, snapshots always fire, and a future undo stack
  hooks in at exactly one place.

## Snapshot side

`SceneSnapshot.Capture` copies ids, names, transforms, counts — cheap (a scene here is
tens of nodes, not thousands). It's published only when a command actually mutated
something, not per frame. `EngineService` re-raises it via `Dispatcher.UIThread.Post`;
`MainViewModel` then rebuilds its collections inside a `_syncing` guard (see
[05_Avalonia_Shell.md](05_Avalonia_Shell.md) for the echo problem).

If scenes grow large, switch from rebuild-the-ObservableCollection to diffing by id —
the snapshot record already carries everything needed.

## Shutdown (the only rendezvous)

`EngineService.Shutdown()` enqueues `ShutdownCommand`, then `Thread.Join(5 s)`. The loop
also exits when raylib's window closes (`WindowShouldClose`), which flows back as the
`Stopped` event → app shutdown. Teardown (meshes, render textures, ImGui, window) happens
on the render thread, after the loop, in reverse init order.

## Pitfalls this design already dodges — keep dodging them

1. **Don't** call any `Raylib.*` / `ImGui.*` from the UI thread (crashes or silent
   corruption). If the shell needs a new engine capability, add a command.
2. **Don't** hand live `SceneNode` references across threads. Extend `SceneSnapshot`
   instead when the UI needs more data.
3. **Don't** block the render thread on I/O in `Apply`. Import currently reads the file
   synchronously (fine for a sample); production path: parse Assimp on a worker thread,
   enqueue a completion command that only does the GPU upload.
4. **Watch dt-dependent logic**: panels run per render frame; anything time-based should
   use `Raylib.GetFrameTime()`, not frame counts.
