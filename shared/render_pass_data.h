#pragma once
#include <cstdint>

// Layout du constant buffer "renderPassData" d'Assetto Corsa EVO (slot b1 du
// vertex shader de la passe couleur principale "Colour Pass").
//
// Reconstitue d'apres la reflexion des shaders capturee avec RenderDoc (les
// shaders embarquent leurs noms de champs). Taille reflechie ~1456 octets.
//
// C'est LE buffer a manipuler pour injecter une camera alternative : remplacer
// viewProjection / view / projection / inverseViewProjection / cameras
// positions, puis relancer le culling GPU + les draws indirects.
//
// ATTENTION: offsets a confirmer champ par champ avec RenderDoc avant usage en
// production. Les matrices font 64 octets (float4x4). Les champs marques TODO
// n'ont pas encore ete dimensionnes precisement.

namespace acevo_obs {

struct alignas(16) Float4x4 { float m[16]; };
struct Float3 { float x, y, z; };

#pragma pack(push, 4)
struct RenderPassData {
    uint32_t entityTilesIndex;          // 0
    uint32_t entityTilesPad[3];         // 4  (padding)

    Float4x4 viewProjection;            // 16   <-- LA cible principale
    Float4x4 view;                      // 80
    Float4x4 prevView;                  // 144
    Float4x4 projection;                // 208  (jittered, utilisee)
    Float4x4 projectionUnjittered;      // 272
    Float4x4 prevProjection;            // 336
    Float4x4 prevProjectionUnjittered;  // 400
    Float4x4 inverseViewProjection;     // 464

    Float3   absoluteCameraPosition;    // 528  ex: 4020.14, 571.08, 4853.51
    float    cameraPad;                 // 540
    Float3   relativeCameraPosition;    // 544
    float    nearPlane;                 // 556  0.10
    Float3   cameraDirection;           // 560  -0.833, 0.020, -0.553
    float    farPlane;                  // 572  15000

    float    zbufferParams[4];          // 576  (reversed-Z params)
    Float3   cameraPositionDeltaWithPreviousFrame; // 592
    float    cameraDeltaPad;            // 604
    float    screenSize[2];             // 608  2586 x 1082
    uint32_t numLights;                 // 616  8
    float    msaaSampleCount;           // 620  1.0

    // --- Suite: sondes de reflexion (tailles a confirmer), non critiques
    //     pour l'override camera. A mapper si besoin :
    //     skyReflectionProbe / vehicleReflectionProbe / cockpitReflectionProbe
    //     reflectionProbesCount (22), reflectionProbeCockpitAvaiable (1),
    //     vehicleReflectionProbeZBufferParams (0.9999,0.0001,99.99,0.01),
    //     screenSpaceShadowEnabled (0), sampleHeroShadowOnEnvironment (1)
};
#pragma pack(pop)

} // namespace acevo_obs
