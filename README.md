# acevo-obs-plugin

⚠️⚠️⚠️VibeCoded This for a friend i don't know ball about anything feel free to fork & make it better or pull request⚠️⚠️⚠️

My discord: porc_ypic

PREVIEW:
https://i.imgur.com/jSleRGM.mp4

Capture multiple camera feeds from **a single instance of Assetto Corsa EVO** into
**OBS Studio**.

The project injects a DLL into the game (DirectX 12 engine), intercepts rendering,
copies selected render targets into shared textures, and an OBS plugin displays them
as independent video sources.

Currently available feeds (4 independent OBS sources):

| OBS source | Game texture | Resolution | Notes |
| --- | --- | --- | --- |
| **Main view (no HUD)** | swapchain backbuffer | native (e.g. 3440×1440) | Tonemapped scene, captured before HUD composite |
| **Mirror — center** | `mirror_texture0` | 1024×256 | Rear-view mirror; Colour Pass #7 + #8 (LOD) |
| **Mirror — left** | `mirror_texture1` | 512×256 | Left side mirror |
| **Mirror — right** | `mirror_texture2` | 512×256 | Right side mirror |

Mirror textures are HDR (`R11G11B10_FLOAT`); the DLL tonemaps them to RGBA8 for OBS.
Each mirror is copied when its render target leaves `RENDER_TARGET` state (last write
of the frame wins).

> ⚠️ Personal modding / reverse-engineering project. See [Warnings](#warnings).

---

## Architecture

```
┌────────────────────────────┐         shared memory              ┌──────────────────┐
│  AssettoCorsaEVO.exe (DX12)│   (Local\acevo_obs_ipc_v1)         │   OBS Studio     │
│                            │  ───────────────────────────────▶  │                  │
│  dxgi.dll (proxy + hooks)  │   KMT handles of shared D3D11      │  acevo-obs.dll   │
│   • Present                │   textures (4 sources)             │  (video source)  │
│   • OMSetRenderTargets     │                                    │                  │
│   • CreateRenderTargetView │                                    │  ┌────────────┐  │
│   • ExecuteCommandLists    │                                    │  │Camera menu │  │
│   • D3D11On12 + tonemap    │                                    │  └────────────┘  │
└────────────────────────────┘                                    └──────────────────┘
```

- **`dxgi.dll`** — a proxy DLL placed next to the game executable. Windows loads it
  **at startup** (before the engine creates its resources), which lets us capture *every*
  `CreateRenderTargetView` call and therefore identify the mirror target. All DXGI calls
  are forwarded to the real system `dxgi.dll` (copied as `dxgi_orig.dll`).
- **Capture** — the DLL uses **D3D11On12** to copy the game's D3D12 render targets into
  shared D3D11 textures (KMT handle), consumable by OBS through `gs_texture_open_shared()`.
- **IPC** — a named shared-memory block (see `shared/ipc.h`) carries, per source, the
  dimensions / format / KMT handle / frame index.

---

## Repository layout

| Path | Purpose |
| --- | --- |
| `injector/dllmain.cpp` | Injected DLL (DX12 hooks, D3D11On12, tonemap, dxgi proxy) |
| `injector/CMakeLists.txt` | DLL build (output: `dxgi.dll`) |
| `obs-plugin/plugin.cpp` | OBS source + camera selection menu |
| `obs-plugin/CMakeLists.txt` | Plugin build (requires libobs) |
| `shared/ipc.h` | Shared-memory structures (DLL ↔ OBS) |
| `shared/render_pass_data.h` | Mapping of the `renderPassData` constant buffer (for future free camera) |
| `tools/inject.cpp` | Manual injector (optional, legacy — no longer needed with the proxy) |
| `CMakeLists.txt` | Root build (FetchContent MinHook) |

---

## Requirements

- Windows 10/11 x64
- Visual Studio 2022 (Build Tools are enough) + CMake ≥ 3.21
- Windows SDK (D3D12, D3D11, D3D11On12, d3dcompiler)
- OBS Studio (tested with **32.0.2**) and its `libobs` headers to build the plugin

---

## Build

### libobs (not vendored)

The OBS plugin builds against `libobs` headers and an `obs.lib` import library, which
are **not** included in this repo (third-party, GPL). The `obs-plugin/CMakeLists.txt`
expects them under `deps/`:

- `deps/obs-src/libobs/` — the `libobs` headers (clone the matching OBS Studio tag, e.g. `32.0.2`)
- `deps/obslib/obs.lib` — import library generated from your local OBS install

```powershell
git clone --depth 1 --branch 32.0.2 https://github.com/obsproject/obs-studio deps/obs-src
# then place obs.lib in deps/obslib/  (see OBS docs to generate it)
```

The injector (`dxgi.dll`) has no such dependency and builds on its own.

### Compile

```powershell
cd E:\Projects\acevo-obs-plugin
cmake -S . -B build -DBUILD_OBS_PLUGIN=ON
cmake --build build --config Release
```

To build only the injector, omit `-DBUILD_OBS_PLUGIN=ON`.

Main outputs:

- `build\injector\Release\dxgi.dll` — the proxy/hook DLL
- `build\obs-plugin\Release\acevo-obs.dll` — the OBS plugin

> `dxgi.dll` stays locked while the game is running. Close the game before rebuilding.

---

## Installation

### 1. The DLL in the game

Copy into the executable's folder (`...\Assetto Corsa EVO\`):

```powershell
$game = 'E:\SteamLibrary\steamapps\common\Assetto Corsa EVO'
# Copy of the real system dxgi (the proxy forwards to it)
Copy-Item -Force 'C:\Windows\System32\dxgi.dll' "$game\dxgi_orig.dll"
# Our proxy
Copy-Item -Force 'build\injector\Release\dxgi.dll' "$game\dxgi.dll"
```

### 2. The plugin in OBS

```powershell
Copy-Item -Force 'build\obs-plugin\Release\acevo-obs.dll' `
  'C:\Program Files\obs-studio\obs-plugins\64bit\acevo-obs.dll'
```

---

## Usage

1. Launch **OBS**.
2. Launch **the game** normally (the proxy loads automatically; a log console opens).
3. Enter a session.
4. In OBS: *Sources → Add → "Assetto Corsa EVO (camera)"*.
5. In the source properties, pick a feed from the **Camera** dropdown:
   - **Main view (no HUD)**
   - **Mirror — center**
   - **Mirror — left**
   - **Mirror — right**

   Create one OBS source per feed (e.g. 4 sources for the full setup).

### Uninstall / revert to vanilla

Just delete `dxgi.dll` and `dxgi_orig.dll` from the game folder.

---

## Technical notes

- **Swapchain detection** — the backbuffer RTV handle is *learned* at `Present` time
  (the last bound RTV); the swapchain → other-RT switch then triggers the copy of the
  clean scene (post-tonemap, pre-HUD).
- **Mirror detection** — `IsMirrorDesc` matches 2D `R11G11B10_FLOAT` render targets
  (wide aspect, height ≤ 512). Three slots are assigned automatically:
  - **Slot 0 (center)** — width ≥ 768 px (`mirror_texture0`, 1024×256)
  - **Slot 1 (left)** — first 512×256 mirror seen (`mirror_texture1`)
  - **Slot 2 (right)** — second 512×256 mirror seen (`mirror_texture2`)

  Side mirrors share the same dimensions, so left/right are distinguished by order of
  first appearance in the frame. If swapped in OBS, just pick the other source.
- **Mirror tonemap** — mirror RTs are linear HDR; a tiny D3D11 shader (`c/(c+1)` + gamma 2.2)
  converts each to RGBA8 for correct rendering in OBS.
- **IPC protocol** — `kProtocolVersion = 3` in `shared/ipc.h` (`CleanView`, `MirrorCenter`,
  `MirrorLeft`, `MirrorRight`). Bump it whenever the layout changes.

---

## Status

- [x] Main view without HUD
- [x] Load at startup via `dxgi.dll` proxy
- [x] Center rear-view mirror
- [x] Left + right side mirrors (3 mirror sources)
- [x] Camera selector in the OBS plugin
- [ ] Full-screen free camera (override `renderPassData`)

---

## Warnings

- **Personal / offline use.** A proxy `dxgi.dll` injects code into the game process.
  AC EVO does not appear to ship a kernel-level anti-cheat as of today, but that **may
  change** during early access. **Do not use in multiplayer**: remove `dxgi.dll` /
  `dxgi_orig.dll` before any online session.
- **Reversible.** Everything is non-destructive: deleting both DLLs restores the original game.
- Not affiliated with Kunos Simulazioni.
