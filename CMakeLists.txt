cmake_minimum_required(VERSION 3.20)

project(NanquimoriEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Allow user to point to a local raylib checkout
set(RAYLIB_PATH "" CACHE PATH "Path to raylib repository (for add_subdirectory)")
if (NOT RAYLIB_PATH)
    if (DEFINED ENV{RAYLIB_PATH} AND EXISTS "$ENV{RAYLIB_PATH}/CMakeLists.txt")
        set(RAYLIB_PATH "$ENV{RAYLIB_PATH}")
    elseif (EXISTS "${PROJECT_SOURCE_DIR}/third_party/raylib/CMakeLists.txt")
        set(RAYLIB_PATH "${PROJECT_SOURCE_DIR}/third_party/raylib")
    elseif (EXISTS "C:/raylib/raylib/CMakeLists.txt")
        set(RAYLIB_PATH "C:/raylib/raylib")
    endif()
endif()

# Collect sources
file(GLOB_RECURSE NANQUIMORI_COMMON_SOURCES
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/app/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/editor/ui/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/editor/viewport/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/physics/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/scene/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/assets/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/runtime/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/tools/*.cpp"
)

set(NANQUIMORI_EDITOR_MAIN "${PROJECT_SOURCE_DIR}/src/app/main.cpp")
set(NANQUIMORI_RUNTIME_MAIN "${PROJECT_SOURCE_DIR}/src/runtime/game_main.cpp")
list(REMOVE_ITEM NANQUIMORI_COMMON_SOURCES "${NANQUIMORI_EDITOR_MAIN}")
list(REMOVE_ITEM NANQUIMORI_COMMON_SOURCES "${NANQUIMORI_RUNTIME_MAIN}")

add_executable(NanquimoriEngine ${NANQUIMORI_COMMON_SOURCES} "${NANQUIMORI_EDITOR_MAIN}")
if (WIN32)
    add_executable(NanquimoriPlayer WIN32 ${NANQUIMORI_COMMON_SOURCES} "${NANQUIMORI_RUNTIME_MAIN}")
else()
    add_executable(NanquimoriPlayer ${NANQUIMORI_COMMON_SOURCES} "${NANQUIMORI_RUNTIME_MAIN}")
endif()

foreach(target NanquimoriEngine NanquimoriPlayer)
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )
endforeach()

# Try to use local raylib checkout if present; otherwise expect a system install
if (EXISTS "${RAYLIB_PATH}/CMakeLists.txt")
    add_subdirectory("${RAYLIB_PATH}" "${CMAKE_BINARY_DIR}/raylib")
    foreach(target NanquimoriEngine NanquimoriPlayer)
        target_link_libraries(${target} PRIVATE raylib)
    endforeach()
else()
    find_package(raylib CONFIG REQUIRED)
    foreach(target NanquimoriEngine NanquimoriPlayer)
        target_link_libraries(${target} PRIVATE raylib)
    endforeach()
endif()

# Windows-specific settings
if (WIN32)
    enable_language(RC)
    function(nanquimori_attach_icon target icon_path)
        if (EXISTS "${icon_path}")
            set(NANQUIMORI_APP_ICON "${icon_path}")
            set(NANQUIMORI_APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/${target}_icon.rc")
            configure_file(
                "${PROJECT_SOURCE_DIR}/src/app/nanquimori_icon.rc.in"
                "${NANQUIMORI_APP_ICON_RC}"
                @ONLY
            )
            target_sources(${target} PRIVATE "${NANQUIMORI_APP_ICON_RC}")
        else()
            message(WARNING "Windows icon not found: ${icon_path}")
        endif()
    endfunction()

    nanquimori_attach_icon(NanquimoriEngine "${PROJECT_SOURCE_DIR}/icons/N.ico")
    nanquimori_attach_icon(NanquimoriPlayer "${PROJECT_SOURCE_DIR}/icons/N.ico")

    foreach(target NanquimoriEngine NanquimoriPlayer)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(${target} PRIVATE d2d1 d3d11 dxgi shlwapi ole32)
    endforeach()
endif()
