# TESTING — AnimForgeLiveLink

Three test layers, one contract: the wire protocol and the warp math are
implemented twice (shared C++ compiled into both plugins, Python for the mock
server and tooling), so every behavioral invariant is asserted in **both**
languages plus **inside the engine**.

## 1. Python suite — runs anywhere, right now

```bat
python AnimForgeLiveLink\Tests\Python\run_all.py
```

56 tests, all passing. Also runs under `mayapy` for in-DCC validation. Covers:

| File | What it pins down |
|---|---|
| `test_protocol.py` | Frame encode/decode; byte-by-byte fragmentation; multiple frames per read; resync after garbage; corrupt length fields; empty payloads; split magic. Envelope round trips for every message type; version/type mismatch rejection; required-field validation; unicode payloads. |
| `test_skewwarp.py` | Every invariant in THEORY.md §6: endpoint exactness, shear property, pre/post-window behavior, degenerate in-place fallback, Scale semantics, progressive rotation correction, empty/single-sample edge cases. |
| `test_session.py` | Evaluate-button logic without Qt/Maya: settings validation (inverted ranges, window outside range, unknown method, missing target/clip/character, bad port, nothing-to-warp), request assembly, MEL command string generation. |
| `test_mock_server.py` | **Integration**: real sockets against `mock_unreal_server.py` on an ephemeral port — handshake, evaluate round trip (endpoint lands on target through the full encode→TCP→decode→warp→encode→TCP→decode path), graceful failure without root samples, error reply to malformed payloads. |

## 2. Standalone C++ suite — shared code, engine-free

```bat
AnimForgeLiveLink\Tests\Cpp\build_and_run.bat     (from a VS dev prompt; finds cl/clang++/g++)
```

113 checks, all passing (verified with MSVC 14.51 / VS 2026). Exercises the
exact translation units both plugins compile: JSON round trips + escape/unicode
handling + malformed-input rejection, framing (fragmentation, resync, corrupt
length), `EvaluateRequest`/`EvaluateResult` round trips with validation, and the
full skew-warp invariant set.

## 3. UE Automation tests — inside the gym binary

`Plugins/AnimForgeUnrealWarpViz/Source/AnimForgeUnrealWarpViz/Private/Tests/`,
under the `AnimForge.WarpViz.*` category (SmokeFilter). Run from **Session
Frontend → Automation**, or headless:

```bat
UnrealEditor-Cmd.exe AnimForgeGym.uproject ^
  -ExecCmds="Automation RunTests AnimForge.WarpViz; Quit" -unattended -nopause -nullrhi
```

Protocol framing/round-trip/rejection plus the skew-warp invariants, compiled
with UE's types and flags — guarding against toolchain-specific drift.

## 4. Manual end-to-end pass (no engine required)

1. `python Scripts\mock_unreal_server.py` (from the Maya plugin's Scripts dir).
2. In Maya: load `AnimForgeMayaWarpViz.mll`, `import animforge_warpviz_ui;
   animforge_warpviz_ui.show()`.
3. Connect to `127.0.0.1:46464`; script editor should log the handshake and
   the mock's known clips (`animForgeWarpViz -status`).
4. Create a locator, select it, **Use Selected**, set clip name
   `MM_Vault_Low`, **Evaluate**.
5. Expect: `AnimForgeWarpViz_Result_grp` with the green warped curve ending at
   the locator, gray templated source curve, ghost locators, and the mock's
   "trajectory synthesized" warning in the script editor.

Repeat against the real gym with the editor open on `L_WarpGym` for the true
engine-evaluated loop; additionally verify the root-drift warning by
deliberately offsetting one Maya key.

## Gaps / future work

- **No automated Maya-in-the-loop test yet.** A `mayapy` test that loads the
  `.mll`, drives `animForgeWarpViz -evaluate` against the mock server and
  asserts the result group exists is the next step (needs a Maya install on
  the runner).
- **Evaluator asset tests.** `FWarpVizEvaluator::ExtractRootTrajectory` /
  `MeasureRootDrift` are exposed for automation tests but need a small test
  animation asset checked into the gym project before they can be asserted.
- **Parity harness vs. the runtime modifier**: replay the same request through
  `URootMotionModifier_SkewWarp` in PIE and diff against the offline result.
