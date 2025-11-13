SRL-Mandelbrot
===============

A small demo project that renders the Mandelbrot fractal using the SRL (Saturn Ring Lib) framework. It targets the Saturn/Emulator environment using VDP1 textures and (optionally) a Slave SH2 worker for offloading pixel iteration work.

Contents
--------
- src/main.cxx — application's source.
- makefile, compile.bat, compile scripts — build helpers.
- BuildDrop/ — packaging and release artifacts.

Quick overview
--------------
The renderer is implemented in `src/main.cxx` and contains:
- `Palette` — a small palette helper derived from `SRL::Bitmap::Palette`.
- `Canvas` — implements `SRL::Bitmap::IBitmap`, holds the 8-bit indexed image buffer and the `BitmapInfo` used for VDP1.
- `MandelbrotParameters<T>` — templated structure that stores complex co-ordinates and pixel coords.
- `MandelbrotRenderer<RealT>` — templated renderer (default `RealT = Fxp`) that progressively computes the fractal and copies the image to VDP1.
- `SlaveTask<RealT>` — (optional) task wrapper inheriting from `SRL::Types::ITask` to run computations on the Slave SH2.

Template support
----------------
The renderer and parameter types are templated so the implementation can run with either the project's fixed-point `Fxp` type (default) or `float` for faster iteration/testing. Example: `MandelbrotRenderer<float> renderer;`.

Build
-----
On Linux (recommended):

```bash
# Build using the included makefile
make
```

On Windows (project includes batch helpers):

```powershell
# Debug build
./compile.bat debug

# Release build
./compile.bat release
```

If you use VS Code, the workspace tasks include "Compile [DEBUG]" and "Compile [RELEASE]" which call the batch scripts.

Run
---
There are helper scripts to run built images in emulators under `run_with_mednafen.bat` and `run_with_kronos.bat`. On Linux you can use any supported emulator that accepts the generated CUE/BIN output under `BuildDrop/`.

Project notes / known issues
---------------------------
- The code was refactored to templatize `MandelbrotParameters` and `MandelbrotRenderer`. The default template parameter keeps the original behaviour using `Fxp`.
- `SlaveTask` is implemented and wired to `SRL::Slave::ExecuteOnSlave()` which calls the SL library to run tasks on the Slave SH2. The code uses `ITask::IsDone()`/`IsRunning()` naming as defined in `srl_slave.hpp`.
- There is a known typo in `src/main.cxx`: a stray `assert(canvasTextureId = > 0 && ...)` should be corrected to use `>=` — the build will fail until that is fixed.

Troubleshooting
---------------
- If compilation fails, read the compiler output and fix the first error reported. Common issues:
  - Missing or mis-typed method names when integrating with SRL types (case-sensitive).
  - Template ordering: some template method definitions are placed after the dependent template types.

- To switch to floating-point rendering for quick testing, instantiate the renderer in `main()` as:

```cpp
static MandelbrotRenderer<float> *g_renderer = nullptr;
g_renderer = new MandelbrotRenderer<float>();
```

![Alt text](pics/Mandelbrot_10_11_2025-20_34_44.png?raw=true "Kronos v2.7.0")

![Alt text](pics/ScreencastFrom2025-11-1221-00-391-ezgif.com-optimize.gif?raw=true "Kronos v2.7.0")