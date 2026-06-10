add_library(acevo_injector SHARED
    dllmain.cpp
)

target_include_directories(acevo_injector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Do not link dxgi directly: our DLL is named dxgi.dll (proxy) and loads the real
# one via dxgi_orig.dll at runtime.
target_link_libraries(acevo_injector PRIVATE
    minhook
    d3d12
    d3d11
    d3dcompiler
)

set_target_properties(acevo_injector PROPERTIES
    OUTPUT_NAME "dxgi"
    PREFIX ""
)
