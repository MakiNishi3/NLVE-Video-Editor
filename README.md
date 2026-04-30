# vidster
Vidster is a non-linear video editor
<img width="1024" height="1024" alt="45949" src="https://github.com/user-attachments/assets/7f2e02f6-9878-4eac-a3f1-b4150e27adf5" />
2,594-line single-file C++17 non-linear video editor. Here's what's included:

**Core architecture:**
- `Pixel`, `Frame`, `MediaItem`, `Clip`, `Track`, `Layer`, `TimelineState` — full data model
- `NLVE` — main editor class wiring everything together

**All 43 video effects** as concrete `VideoEffect` subclasses with full per-pixel math — Mandelbrot/Julia generators use iterative escape-time, Fourier Transform does a full DFT, Kaleido does polar segment mirroring, Warhol does tinted grid compositing, etc.

**Functions implemented:**
- `uploadMedia` — validates path, loads into media pool, generates a synthetic frame
- `exportMedia` — renders every frame via compositor, writes raw RGBA binary + header
- File menu: `newProject`, `saveProject`, `openProject` — custom text serialization format `NLVE_PROJECT_V1`
- Edit menu: `undoAction`, `redoAction`, `copyClips`, `cutClips`, `pasteClips`, `splitClip` — all via command pattern with 100-deep undo stack
- `loadOpenFXForWin` / `loadOpenFXForMacOS` / `loadOpenFXForLinux` — walks standard OFX plugin directories, `LoadLibrary`/`dlopen` per platform
- `addVideoEffect` / `removeVideoEffect` with parameter maps
- `addTrack` / `removeTrack`, `addLayer` / `removeLayer`

**Build:** `g++ -std=c++17 -O2 nlve.cpp -o nlve` on Linux/macOS; on Windows add `-lole32` if needed.
