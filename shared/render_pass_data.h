#pragma once
#include <cstdint>

// Layout of the "renderPassData" constant buffer in Assetto Corsa EVO (slot b1
// of the main colour pass vertex shader).
//
// Reconstructed from shader reflection captured with RenderDoc (shaders embed
// field names). Reflected size ~1456 bytes.
//
// This is THE buffer to manipulate for an alternate camera: replace
// viewProjection / view / projection / inverseViewProjection / camera
// positions, then re-run GPU culling + indirect draws.
//
// WARNING: confirm offsets field-by-field with RenderDoc before production use.
// Matrices are 64 bytes (float4x4). Fields marked TODO are not yet sized.

namespace acevo_obs {

struct alignas(16) Float4x4 { float m[16]; };
struct Float3 { float x, y, z; };

#pragma pack(push, 4)
struct RenderPassData {
    uint32_t entityTilesIndex;          // 0
    uint32_t entityTilesPad[3];         // 4  (padding)

    Float4x4 viewProjection;            // 16   <-- primary override target
    Float4x4 view;                      // 80
    Float4x4 prevView;                  // 144
    Float4x4 projection;                // 208  (jittered, in use)
    Float4x4 projectionUnjittered;      // 272
    Float4x4 prevProjection;            // 336
    Float4x4 prevProjectionUnjittered;  // 400
    Float4x4 inverseViewProjection;     // 464

    Float3   absoluteCameraPosition;    // 528  e.g. 4020.14, 571.08, 4853.51
    float    cameraPad;                 // 540
    Float3   relativeCameraPosition;    // 544
    float    nearPlane;                 // 556  0.10
    Float3   cameraDirection;           // 560  -0.833, 0.020, -0.553
    float    farPlane;                  // 572  15000

    float    zbufferParams[4];          // 576  (reversed-Z params)
    Float3   cameraPositionDeltaWithPreviousFrame; // 592
    float    cameraDeltaPad;            // 604
    float    screenSize[2];             // 608  e.g. 2586 x 1082
    uint32_t numLights;                 // 616  8
    float    msaaSampleCount;           // 620  1.0

    // --- Remaining fields: reflection probes (sizes TBD), not critical for
    //     camera override. Map if needed:
    //     skyReflectionProbe / vehicleReflectionProbe / cockpitReflectionProbe
    //     reflectionProbesCount (22), reflectionProbeCockpitAvaiable (1),
    //     vehicleReflectionProbeZBufferParams (0.9999,0.0001,99.99,0.01),
    //     screenSpaceShadowEnabled (0), sampleHeroShadowOnEnvironment (1)
};
#pragma pack(pop)

} // namespace acevo_obs
