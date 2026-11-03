# FLOWALIGN 0.5.0 verification

Compiled source commit: `de67072328250bf157ffba5b67e427ab97457a26`. JUCE: `454fe891acc8204f99f4ef1e3a28c84b1a4da145`. The release documentation update changes no compiled source or packaging logic.

| Check | Russian | English |
|---|---|---|
| Full local processor regression | PASS | PASS |
| [Independent macOS CI](https://github.com/Abynarc/FLOWALIGN/actions/runs/41633806853) | PASS | PASS |
| [Windows x64 CI](https://github.com/Abynarc/FLOWALIGN/actions/runs/41633806797) | PASS | PASS |

UI checks exercise the actual dropdown in both directions, independent instances, no processing-parameter changes, translated buttons/menus/tooltips, 1000/1280/1600-pixel layouts, editor recreation, state loading with an open editor, and old projects without a language property. Catalog coverage: 226 entries. Composed import errors are checked for complete English wording while preserving durations; filenames remain untouched.

The processor suite covers capture/sidechain/transport, mono/stereo and sample rates, eight pairs, alignment, pitch/stereo preservation, cancellation, imports, WAV/CSV export, playback, concurrent processing and state compatibility. Switching language during armed capture and with completed aligned audio retains the capture and rendered output. Synthetic alignment timing error: 112 ms before, 3.5 ms after, in each language run.

macOS PKG: arm64 + x86_64, minimum macOS 12. Package expansion, signed-payload hashes, ad-hoc signature, install/upgrade scripts on an isolated volume, system and per-user backups, and corrupted-payload rejection passed. The built AU was loaded directly and checked for both input buses and bit-exact stereo passthrough. Installed system plug-ins were not replaced for these checks.

Windows: installer build, install, byte comparison, upgrade backup, pluginval strictness 5, uninstall and backup retention passed on a Windows runner. RU/EN screenshots from that runner were also inspected.

Limits: no manual listening or interactive Logic/FL Studio session was performed. The Intel slice was cross-compiled; native Intel Mac hardware was not tested. The macOS package is not Developer ID signed/notarized; Windows has no publisher certificate. Host automation parameter names retain their existing registration independently of the custom editor language.
