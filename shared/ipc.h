#pragma once
#include <cstdint>

// Shared IPC protocol between the injected DLL (game side, D3D12) and the OBS
// plugin (D3D11).
//
// The DLL uses D3D11On12 to copy the game's D3D12 backbuffer into a *shared*
// D3D11 texture (legacy KMT handle via IDXGIResource::GetSharedHandle). OBS
// opens it with gs_texture_open_shared(). The DLL publishes the handle and
// dimensions here.

namespace acevo_obs {

inline constexpr wchar_t kSharedMemoryName[] = L"Local\\acevo_obs_ipc_v1";
inline constexpr uint32_t kProtocolVersion = 3;

enum class SourceId : uint32_t {
    CleanView    = 0,  // main view without HUD
    MirrorCenter = 1,  // rear-view mirror (mirror_texture0, ~1024x256)
    MirrorLeft   = 2,  // left mirror  (512x256, first 512-wide seen)
    MirrorRight  = 3,  // right mirror (512x256, second 512-wide seen)
    Count
};

#pragma pack(push, 4)
struct SharedTextureDesc {
    uint32_t width;
    uint32_t height;
    uint32_t dxgiFormat;     // DXGI_FORMAT of the shared texture
    uint32_t valid;          // 0 = not ready, 1 = handle is usable
    uint64_t kmtHandle;      // KMT handle (GetSharedHandle), openable by OBS
    uint64_t frameIndex;     // incremented on each Present
};

struct SharedBlock {
    uint32_t protocolVersion;
    uint32_t pid;
    SharedTextureDesc sources[static_cast<uint32_t>(SourceId::Count)];
};
#pragma pack(pop)

} // namespace acevo_obs
