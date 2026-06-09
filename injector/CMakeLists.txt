add_library(acevo_injector SHARED
    dllmain.cpp
)

target_include_directories(acevo_injector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Pas de lien direct vers dxgi : notre DLL s'appelle dxgi.dll (proxy) et charge
# le vrai via dxgi_orig.dll a l'execution.
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
