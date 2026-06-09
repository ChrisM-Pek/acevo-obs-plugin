cmake_minimum_required(VERSION 3.21)
project(acevo_obs_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "Projet Windows uniquement (DX12 + injection).")
endif()

# MinHook est requis pour la DLL injectee. Recupere via FetchContent.
include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG master
)
FetchContent_MakeAvailable(minhook)

add_subdirectory(injector)

add_executable(inject tools/inject.cpp)

# Le plugin OBS necessite le SDK obs-studio (libobs). Active-le quand le
# chemin vers libobs est fourni : -DLIBOBS_DIR=...
option(BUILD_OBS_PLUGIN "Compiler le plugin OBS (necessite libobs)" OFF)
if(BUILD_OBS_PLUGIN)
    add_subdirectory(obs-plugin)
endif()
