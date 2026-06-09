// Plugin OBS Studio : affiche la texture partagee publiee par la DLL injectee
// dans le jeu (palier 2 : vue du backbuffer).

#include <obs-module.h>
#include <graphics/graphics.h>
#include <windows.h>

#include "../shared/ipc.h"

using namespace acevo_obs;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("acevo-obs", "en-US")

namespace {

struct AcevoSource {
    obs_source_t* source = nullptr;
    HANDLE        mapping = nullptr;
    SharedBlock*  shared = nullptr;
    gs_texture_t* texture = nullptr;
    uint64_t      openedHandle = 0;
    uint32_t      width = 0;
    uint32_t      height = 0;
    uint32_t      sourceId = (uint32_t)SourceId::CleanView;
};

const char* SourceName(void*) { return "Assetto Corsa EVO (camera)"; }

void SourceUpdate(void* data, obs_data_t* settings) {
    auto* s = static_cast<AcevoSource*>(data);
    uint32_t newId = (uint32_t)obs_data_get_int(settings, "camera");
    if (newId != s->sourceId) {
        s->sourceId = newId;
        s->openedHandle = 0;  // force la reouverture de la texture
    }
}

void* SourceCreate(obs_data_t* settings, obs_source_t* source) {
    auto* s = new AcevoSource();
    s->source = source;
    s->mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kSharedMemoryName);
    if (s->mapping) {
        s->shared = static_cast<SharedBlock*>(
            MapViewOfFile(s->mapping, FILE_MAP_READ, 0, 0, sizeof(SharedBlock)));
    }
    SourceUpdate(s, settings);
    return s;
}

obs_properties_t* SourceProperties(void*) {
    obs_properties_t* props = obs_properties_create();
    obs_property_t* list = obs_properties_add_list(
        props, "camera", "Camera",
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(list, "Vue principale (sans HUD)", (long long)SourceId::CleanView);
    obs_property_list_add_int(list, "Retroviseur", (long long)SourceId::Camera1);
    return props;
}

void SourceDefaults(obs_data_t* settings) {
    obs_data_set_default_int(settings, "camera", (long long)SourceId::CleanView);
}

void SourceDestroy(void* data) {
    auto* s = static_cast<AcevoSource*>(data);
    obs_enter_graphics();
    if (s->texture) gs_texture_destroy(s->texture);
    obs_leave_graphics();
    if (s->shared) UnmapViewOfFile(s->shared);
    if (s->mapping) CloseHandle(s->mapping);
    delete s;
}

// (Re)ouvre la texture partagee si le handle a change.
void EnsureTexture(AcevoSource* s) {
    // Si la memoire partagee n'existait pas encore a la creation de la source
    // (DLL pas encore injectee), on reessaie ici a chaque frame.
    if (!s->shared) {
        if (!s->mapping)
            s->mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kSharedMemoryName);
        if (s->mapping && !s->shared)
            s->shared = static_cast<SharedBlock*>(
                MapViewOfFile(s->mapping, FILE_MAP_READ, 0, 0, sizeof(SharedBlock)));
    }
    if (!s->shared) return;
    const auto& src = s->shared->sources[s->sourceId];
    if (!src.valid || src.kmtHandle == 0) return;
    if (s->texture && s->openedHandle == src.kmtHandle) return;

    obs_enter_graphics();
    if (s->texture) { gs_texture_destroy(s->texture); s->texture = nullptr; }
    s->texture = gs_texture_open_shared((uint32_t)src.kmtHandle);
    obs_leave_graphics();

    if (s->texture) {
        s->openedHandle = src.kmtHandle;
        s->width = src.width;
        s->height = src.height;
        blog(LOG_INFO, "[acevo-obs] texture partagee ouverte %ux%u",
             s->width, s->height);
    }
}

uint32_t SourceWidth(void* data) { return static_cast<AcevoSource*>(data)->width; }
uint32_t SourceHeight(void* data) { return static_cast<AcevoSource*>(data)->height; }

void SourceTick(void* data, float) { EnsureTexture(static_cast<AcevoSource*>(data)); }

void SourceRender(void* data, gs_effect_t*) {
    auto* s = static_cast<AcevoSource*>(data);
    if (!s->texture) return;

    gs_effect_t* effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t* image = gs_effect_get_param_by_name(effect, "image");
    gs_effect_set_texture(image, s->texture);
    while (gs_effect_loop(effect, "Draw")) {
        gs_draw_sprite(s->texture, 0, s->width, s->height);
    }
}

obs_source_info MakeInfo() {
    obs_source_info info = {};
    info.id = "acevo_camera_source";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name = SourceName;
    info.create = SourceCreate;
    info.destroy = SourceDestroy;
    info.get_width = SourceWidth;
    info.get_height = SourceHeight;
    info.video_tick = SourceTick;
    info.video_render = SourceRender;
    info.get_properties = SourceProperties;
    info.get_defaults = SourceDefaults;
    info.update = SourceUpdate;
    return info;
}

} // namespace

bool obs_module_load(void) {
    static obs_source_info info = MakeInfo();
    obs_register_source(&info);
    blog(LOG_INFO, "[acevo-obs] plugin charge");
    return true;
}
