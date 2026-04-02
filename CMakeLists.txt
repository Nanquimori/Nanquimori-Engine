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
file(GLOB_RECURSE NANQUIMORI_SOURCES
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/app/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/editor/ui/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/editor/viewport/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/physics/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/scene/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/assets/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/tools/*.cpp"
)

add_executable(NanquimoriEngine ${NANQUIMORI_SOURCES})

target_include_directories(NanquimoriEngine PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)

# Try to use local raylib checkout if present; otherwise expect a system install
if (EXISTS "${RAYLIB_PATH}/CMakeLists.txt")
    add_subdirectory("${RAYLIB_PATH}" "${CMAKE_BINARY_DIR}/raylib")
    target_link_libraries(NanquimoriEngine PRIVATE raylib)
else()
    find_package(raylib CONFIG REQUIRED)
    target_link_libraries(NanquimoriEngine PRIVATE raylib)
endif()

# Windows-specific settings
if (WIN32)
    enable_language(RC)
    set(NANQUIMORI_APP_ICON "${PROJECT_SOURCE_DIR}/icons/N.ico")
    if (EXISTS "${NANQUIMORI_APP_ICON}")
        set(NANQUIMORI_APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/nanquimori_icon.rc")
        configure_file(
            "${PROJECT_SOURCE_DIR}/src/app/nanquimori_icon.rc.in"
            "${NANQUIMORI_APP_ICON_RC}"
            @ONLY
        )
        target_sources(NanquimoriEngine PRIVATE "${NANQUIMORI_APP_ICON_RC}")
    else()
        message(WARNING "Windows icon not found: ${NANQUIMORI_APP_ICON}")
    endif()
    target_compile_definitions(NanquimoriEngine PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
