# XCSoar Copilot Instructions

## Build, test, and update commands

- The normal local development target is `UNIX`. Use parallel builds and ccache:
  - `make -j$(nproc) TARGET=UNIX USE_CCACHE=y`
- Build the main app, optional outputs, debug tools, unit tests, and harness tools:
  - `make -j$(nproc) TARGET=UNIX USE_CCACHE=y everything`
- Run the full unit test suite:
  - `make -j$(nproc) TARGET=UNIX USE_CCACHE=y check`
- Run only the fast or slow harness subsets:
  - `make -j$(nproc) TARGET=UNIX USE_CCACHE=y testfast`
  - `make -j$(nproc) TARGET=UNIX USE_CCACHE=y testslow`
- Build and run a single test by targeting its generated binary in `output/<TARGET>/bin/`:
  - `make TARGET=UNIX output/UNIX/bin/TestGeoPoint`
  - `output/UNIX/bin/TestGeoPoint`
- Use a warnings-as-errors build as the closest thing to a lint pass:
  - `make -j$(nproc) TARGET=UNIX USE_CCACHE=y WERROR=y`
- The CI sanitizer configuration for native builds is:
  - `make -j$(nproc) TARGET=UNIX VFB=y SANITIZE=y DEBUG_GLIBCXX=y everything check`
- After changing user-visible strings, refresh translation catalogs:
  - `make update-po`
- When editing manuals or developer docs, rebuild them with:
  - `make manual`

## High-level architecture

- XCSoar is a layered C++20 application. The low-level foundation lives in `src/util/`, `src/Math/`, and `src/Geo/`; glide/task/airspace/route logic lives in `src/Engine/`; runtime sensor processing and calculations live in `src/Device/`, `src/Blackboard/`, `src/Computer/`, `src/MergeThread.cpp`, and `src/CalculationThread.cpp`; the UI lives in `src/Dialogs/`, `src/Form/`, `src/Renderer/`, `src/MapWindow/`, and `src/Interface.hpp`.
- Global runtime state is split by responsibility: `BackendComponents` owns devices, blackboards, loggers, merge/calculation threads, and replay; `DataComponents` owns loaded terrain, topography, waypoints, and airspaces; `NetComponents` owns tracking and HTTP-related services.
- Sensor data follows a blackboard pipeline: device threads parse into per-device `NMEAInfo`, `MergeThread` combines and enriches it, `CalculationThread` runs heavier glide/task/airspace calculations, and the UI thread receives copies into `InterfaceBlackboard`. `MapWindow` uses its own draw-thread blackboard instead of reading directly from backend state.
- UI code reads state through `CommonInterface::*()` and pushes changes through `ActionInterface::*()`. Backend code should not touch dialogs or other UI classes directly; backend-to-UI notifications are queued through input events instead.
- Platform abstraction is spread across `src/ui/` plus platform-specific directories such as `src/Android/`, `src/Apple/`, `src/Kobo/`, and the target-specific `src/ui/window/*` and `src/ui/canvas/*` implementations. Keep shared behavior in common code and isolate platform differences in the platform override files.

## Key conventions

- XCSoar uses UTF-8 everywhere. New code should prefer `char`, `std::string`, and `std::string_view`; avoid introducing new `TCHAR`/`_T()` usage. Use `Path` or `AllocatedPath` for file paths instead of raw C strings.
- User-visible text must use translation macros: `_()` for immediate translation and `N_()` for deferred translation in static tables. Log messages stay in English and are not wrapped for translation. If strings change, run `make update-po`.
- `NMEAInfo` availability state uses `Validity`, not `bool`. Update fields with `field_available.Update(info.clock)`, clear them with `.Clear()`, expire them in `Expire()`, and copy them with `Validity::Complement()` instead of manual flag copying.
- Thread boundaries matter. `CommonInterface::Basic()` / `Calculated()` are UI-thread-only accessors. Device and calculation code should work through the appropriate blackboard and must not call UI helpers directly.
- Tests use the TAP helpers in `test/src/TestUtil.hpp`. `plan_tests()` must exactly match the number of `ok1()` assertions.
- Renderer and look changes must work across all three canvas backends: OpenGL, memory canvas, and Windows GDI. UI sizing should go through `Layout::Scale()` / `PtScale()` / `VptScale()` / `FontScale()` instead of hard-coded pixels, and any new UI must stay usable on Kobo e-paper displays (`HasColors()`, `IsDithered()`).
- For platform-specific UI work, prefer shared implementations in `src/ui/window/custom/` and small per-platform overrides in `src/ui/window/<platform>/` instead of scattering large `#ifdef` blocks through common code.
- Source files use the XCSoar house style: SPDX + copyright header, `#pragma once` in headers, 2-space indentation, 79-column target width, `CamelCase` for types/functions, and `lowercase_with_underscores` for variables.
- If you create commits, use XCSoar-style subjects such as `Device/Driver/Flarm: Fix parser state` or `build/wayland.mk: Fix generated filename`; for source files, omit the leading `src/` from the subject path.
