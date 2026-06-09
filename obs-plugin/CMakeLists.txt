# Compile contre les headers libobs clones (32.0.2) et la lib d'import obs.lib
# generee depuis l'install OBS locale.

set(LIBOBS_INCLUDE "${CMAKE_SOURCE_DIR}/deps/obs-src/libobs")
set(OBS_LIB "${CMAKE_SOURCE_DIR}/deps/obslib/obs.lib")

add_library(acevo_obs_source MODULE
    plugin.cpp
)

target_include_directories(acevo_obs_source PRIVATE
    ${LIBOBS_INCLUDE}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(acevo_obs_source PRIVATE
    "${OBS_LIB}"
)

set_target_properties(acevo_obs_source PROPERTIES
    OUTPUT_NAME "acevo-obs"
    PREFIX ""
)
