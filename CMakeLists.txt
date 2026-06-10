cmake_minimum_required(VERSION 3.21)
project(acevo_obs_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "Windows-only project (DX12 + injection).")
endif()

# MinHook is required for the injected DLL. Fetched via FetchContent.
include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG master
)
FetchContent_MakeAvailable(minhook)

add_subdirectory(injector)

add_executable(inject tools/inject.cpp)

# The OBS plugin requires the obs-studio SDK (libobs). Enable when deps/ is set up:
# cmake -DBUILD_OBS_PLUGIN=ON
option(BUILD_OBS_PLUGIN "Build the OBS plugin (requires libobs)" OFF)
if(BUILD_OBS_PLUGIN)
    add_subdirectory(obs-plugin)
endif()
