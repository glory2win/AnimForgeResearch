# 05 — Avalonia shell

## Role

The Control Center owns everything a native desktop app should: the **File menu**
(New / Open / Import / Save / Save As / Exit with standard shortcuts), file pickers,
confirmation dialogs, precise numeric editing, and the log. It deliberately does **not**
render 3D — see [01_Architecture.md](01_Architecture.md) for why.

## MVVM without a framework

Three small classes instead of a toolkit dependency (`ViewModels/ViewModelBase.cs`):
`ViewModelBase` (INPC + `Set`/`Raise`), `RelayCommand`, `AsyncRelayCommand`.
If the shell grows, swapping to `CommunityToolkit.Mvvm` is mechanical.

Split of responsibilities:

* **`MainViewModel`** — pure state + engine calls. No Avalonia types, fully unit-testable.
* **`MainWindow.axaml.cs`** — anything needing a window handle: `StorageProvider` pickers,
  modal dialogs, shutdown. Exposes the `ICommand`s that both menu items and
  `Window.KeyBindings` bind to (via `{Binding #Root.XxxCmd}`).
* **`EngineService`** — the ONLY place threading appears: wraps `RenderHost` events in
  `Dispatcher.UIThread.Post`.

## The echo problem (and its one-flag fix)

UI edits → command → engine mutates → snapshot → UI updates its own fields → property
setters fire again → infinite loop. `MainViewModel._syncing` breaks it: snapshot handlers
set it while writing to bound properties; setters no-op command sends while it's true.
Any time you add a bound property that pushes a command, respect that flag.

## File dialogs (Avalonia 11 StorageProvider)

```csharp
var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions {
    Title = "Import Mesh", AllowMultiple = true,
    FileTypeFilter = new[] { new FilePickerFileType("Mesh files")
        { Patterns = new[] { "*.fbx", "*.obj", "*.gltf", "*.glb", "*.dae" } } },
});
var path = files.FirstOrDefault()?.TryGetLocalPath();   // null for non-filesystem providers
```

Save-vs-SaveAs logic lives in `OnSave`: empty `ScenePath` ⇒ delegate to `OnSaveAs`.
Dirty-scene confirmation (`ConfirmDiscardIfDirty`) guards New/Open/Exit; Avalonia has no
built-in MessageBox, so `ShowDialog` builds a tiny modal in code.

## Hosting the workspace: `Controls/RaylibHost.cs`

The center of the window is a `NativeControlHost` that adopts the engine's HWND
(details in [01_Architecture.md](01_Architecture.md)). Ordering matters and lives in
`App.axaml.cs`: **start the engine first** (`EngineService.Start` blocks until the raylib
window exists), copy the handle into `RaylibHost.WorkspaceHwnd`, then create MainWindow.
Focus is split by design: keyboard goes to whichever side was clicked last — Avalonia
shortcuts (Ctrl+S) when the shell has focus, viewport keys (F) when the workspace does.

## Shutdown choreography

Both paths join the render thread exactly once:

* **Avalonia exit** (menu or ✕): `desktop.ShutdownRequested` → `EngineService.Shutdown()`
  → enqueue `ShutdownCommand` + `Thread.Join` (5 s cap). `RaylibHost` detaches the child
  HWND on teardown; the engine destroys the window itself via `CloseWindow`.
* **Workspace closed** (only possible in non-embedded fallback mode): render loop exits →
  `Stopped` event → marshaled → `desktop.Shutdown()` (the `_stopping` flag prevents the
  bounce-back).

## Upgrade path: real docking in the shell (Dock.Avalonia)

The sample uses `GridSplitter` columns — reliable and dependency-free. When the shell
needs tearable/tabbed tool windows, add [`Dock.Avalonia`](https://github.com/wieslawsoltes/Dock)
(match its version to your Avalonia version):

1. Packages: `Dock.Avalonia`, `Dock.Model.Mvvm`.
2. Implement a `Factory : Dock.Model.Mvvm.Factory` that builds the layout: a `RootDock`
   containing `ToolDock`s for Outliner/Properties/Log around a center `DocumentDock`.
3. Each current panel becomes a `Tool` (its DataTemplate is the XAML you already have —
   the ListBox/StackPanel bodies move over unchanged, still bound to `MainViewModel`).
4. Replace the center `Grid` in `MainWindow.axaml` with `<DockControl Layout="{Binding Layout}" />`.
5. Persist via `DockSerializer` to a `layout.json` beside user settings.

Nothing in the engine or view models changes — that's the payoff of keeping the shell thin.
