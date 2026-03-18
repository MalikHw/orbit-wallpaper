cmake_minimum_required(VERSION 3.16)
project(orbit_wallpaper)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -s")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(NOT DEFINED APP_VERSION)
    set(APP_VERSION "dev")
endif()
add_compile_definitions(APP_VERSION="${APP_VERSION}")

find_package(SDL2 REQUIRED)
find_package(SDL2_image QUIET)

set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/box2d)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/extern/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp
)

# ── main wallpaper exe ────────────────────────────────────────────────────────
add_executable(orbit_wallpaper WIN32 main.cpp ${IMGUI_SOURCES})
if(EXISTS "${CMAKE_SOURCE_DIR}/icon.rc")
    target_sources(orbit_wallpaper PRIVATE icon.rc)
endif()
target_include_directories(orbit_wallpaper PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends)
target_link_libraries(orbit_wallpaper PRIVATE
    ${SDL2_LIBRARIES}
    SDL2_image::SDL2_image
    box2d
    opengl32
    shell32 comdlg32 gdi32 winmm advapi32 winhttp urlmon
    ole32 oleaut32 uuid)
set_target_properties(orbit_wallpaper PROPERTIES
    OUTPUT_NAME "orbit_wallpaper"
    SUFFIX ".exe")

# ── updater ───────────────────────────────────────────────────────────────────
add_executable(updater WIN32 updater.cpp)
if(EXISTS "${CMAKE_SOURCE_DIR}/icon.rc")
    target_sources(updater PRIVATE icon.rc)
endif()
target_link_libraries(updater PRIVATE
    shell32 ole32 oleaut32 uuid advapi32)
set_target_properties(updater PROPERTIES OUTPUT_NAME "updater")

# ── copy assets post-build ────────────────────────────────────────────────────
foreach(ASSET
    orb1.png orb2.png orb3.png orb4.png orb5.png
    orb6.png orb7.png orb8.png orb9.png orb10.png
    cube.png)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${ASSET}")
        add_custom_command(TARGET orbit_wallpaper POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/${ASSET}"
                "$<TARGET_FILE_DIR:orbit_wallpaper>/${ASSET}")
    endif()
endforeach()
