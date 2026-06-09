#pragma once
#include <cstdint>

// Protocole IPC partagé entre la DLL injectée (côté jeu, D3D12) et le plugin
// OBS (D3D11).
//
// La DLL utilise D3D11On12 pour copier le backbuffer D3D12 du jeu dans une
// texture D3D11 *partagée* (handle KMT legacy obtenu via
// IDXGIResource::GetSharedHandle). Ce handle KMT est lisible par OBS avec
// gs_texture_open_shared(). La DLL publie le handle + les dimensions ici.

namespace acevo_obs {

inline constexpr wchar_t kSharedMemoryName[] = L"Local\\acevo_obs_ipc_v1";
inline constexpr uint32_t kProtocolVersion = 2;

enum class SourceId : uint32_t {
    CleanView = 0,  // backbuffer final (PoC palier 2)
    Camera1   = 1,  // caméra alternative (palier 3, plus tard)
    Camera2   = 2,
    Count
};

#pragma pack(push, 4)
struct SharedTextureDesc {
    uint32_t width;
    uint32_t height;
    uint32_t dxgiFormat;     // DXGI_FORMAT du backbuffer
    uint32_t valid;          // 0 = pas prêt, 1 = handle exploitable
    uint64_t kmtHandle;      // handle KMT (GetSharedHandle), ouvrable par OBS
    uint64_t frameIndex;     // incrémenté à chaque Present
};

struct SharedBlock {
    uint32_t protocolVersion;
    uint32_t pid;
    SharedTextureDesc sources[static_cast<uint32_t>(SourceId::Count)];
};
#pragma pack(pop)

} // namespace acevo_obs
