cmake_minimum_required(VERSION 3.21)
project(ModifierKeyFramework VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CommonLibSSE-NG (alandtse ng, git submodule, pinned to v10.0.0). AE only: 1.6.x and 1.7.x.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/commonlibsse-ng commonlib)

# a plain source add_subdirectory does not define the plugin helper
if(NOT COMMAND add_commonlibsse_plugin)
	include(lib/commonlibsse-ng/cmake/CommonLibSSE.cmake)
endif()

add_commonlibsse_plugin(${PROJECT_NAME}
	NAME "ModifierKeyFramework"
	AUTHOR "eXgamble"
	SOURCES src/plugin.cpp src/Settings.cpp src/Rules.cpp src/Hooks.cpp src/Input.cpp src/Papyrus.cpp src/Hud.cpp)

find_package(nlohmann_json CONFIG REQUIRED)
find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h")
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(${PROJECT_NAME} PRIVATE src ${SIMPLEINI_INCLUDE_DIRS})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_options(${PROJECT_NAME} PRIVATE /Zc:preprocessor /permissive- /EHsc /utf-8 /W4)
target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# Copy the DLL into the mod after every build (the MO2 mod folder is a junction to ../mod).
# The PDB (debug symbols) is not shipped: it stays in build/release for reading crash logs.
set(DEPLOY_DIR "C:/Modding/MO2/mods/Modifier Key Framework/SKSE/Plugins")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DEPLOY_DIR}"
	VERBATIM)
