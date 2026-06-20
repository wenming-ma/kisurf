# Windows Build Tree Launcher Design

## Purpose

Computer Use smoke testing exposed a practical direct-use issue: launching
`out\build\x64-release\pcbnew\pcbnew.exe` directly from Windows can show
`pcbnew.exe - system error` because `kicommon.dll` and sibling build-tree DLLs
are not on the process search path.

KiSurf needs a reliable developer-preview launch entry that starts PCB and
schematic editors from the build tree with the correct runtime environment,
without changing the user's global PATH and without requiring an install step.

## Requirements

1. Add a Windows PowerShell launcher script under `tools/`.
2. The launcher must accept a build directory and an app name.
3. Supported apps for this slice are `pcbnew`, `eeschema`, `kicad`, and
   `qa_common`.
4. The launcher must resolve the executable path inside the build tree and fail
   with a clear message when the build directory or executable is missing.
5. The launcher must set `KICAD_RUN_FROM_BUILD_DIR=1` for the launched process.
6. The launcher must prepend build-tree runtime directories to PATH:
   `kicad`, `common`, `api`, `common\gal`, `pcbnew`, `eeschema`, and `cvpcb`.
7. When available, the launcher must also prepend the vcpkg runtime `bin`
   directory from `CMakeCache.txt`.
8. The launcher must support a dry-run JSON mode so the environment resolution
   can be tested without opening a GUI.
9. README quickstart must point users to the launcher instead of telling them to
   run build-tree executables directly.
10. The existing `tools/post_build.ps1 -SyncBuildTree` remains valid for users
    who prefer to copy DLLs into each build output directory.
11. The launcher must restore the caller PowerShell process environment after
    dry-run output or process launch.

## Non-Goals

- No installed-layout packaging changes.
- No global user or system environment modification.
- No copying DLLs in this slice.
- No launcher support for every KiCad utility.
- No credential or model-settings changes.

## Testing

1. Before creating the script, running the intended dry-run command must fail
   because the script does not exist.
2. After implementation, dry-run JSON for `pcbnew` must include:
   - app `pcbnew`
   - existing executable path
   - `KICAD_RUN_FROM_BUILD_DIR` value `1`
   - PATH entries containing `common`, `api`, `common\gal`, and `pcbnew`
3. Dry-run must leave the caller's `PATH` and `KICAD_RUN_FROM_BUILD_DIR`
   unchanged.
4. Launching `pcbnew` through the script must open the PCB editor without a
   missing-DLL/system-error modal.
5. Computer Use must verify the PCB editor main window opens and the AI Agent
   entry can be reached.

## Self-Review

- Placeholder scan: no unfinished marker remains.
- Scope check: this is a developer-preview launcher, not packaging.
- Safety check: it changes only the child process environment.
- Ambiguity check: direct executable launch is no longer documented as the
  recommended build-tree path.
