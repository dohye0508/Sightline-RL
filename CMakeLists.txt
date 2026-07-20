cmake_minimum_required(VERSION 3.12)
project(RLConstrainedFOV CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─────────────────────────────────────────────────────────────────────────────
# 빌드 옵션
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(USE_OPENCV "OpenCV를 사용하여 비디오 저장을 활성화합니다" ON)

# 소스 파일 목록 정의
set(SOURCES
    src/MathUtils.hpp
    src/Terrain.hpp
    src/Terrain.cpp
    src/Agent.hpp
    src/Agent.cpp
    src/RLEnvironment.hpp
    src/RLEnvironment.cpp
    src/Renderer.hpp
    src/Renderer.cpp
    src/VideoCapture.hpp
    src/VideoCapture.cpp
    src/main.cpp
)

add_executable(rl_constrained_fov ${SOURCES})

# ─────────────────────────────────────────────────────────────────────────────
# SFML 라이브러리 탐색 및 설정
# ─────────────────────────────────────────────────────────────────────────────
# 윈도우 환경에서 SFML 경로를 환경 변수나 CMake 변수로 직접 정의할 수 있도록 유연하게 구성합니다.
# 예: cmake -DSFML_DIR=C:/SFML-2.5.1/lib/cmake/SFML
find_package(SFML 2.5 COMPONENTS graphics window system)

if(SFML_FOUND)
    message(STATUS "SFML Found: ${SFML_VERSION}")
    target_link_libraries(rl_constrained_fov PRIVATE sfml-graphics sfml-window sfml-system)
else()
    message(WARNING "SFML을 find_package로 찾지 못했습니다. 헤더 및 라이브러리 경로 수동 지정을 시도합니다.")
    
    # 일반적인 설치 폴더나 프로젝트 로컬 폴더 탐색
    find_path(SFML_INCLUDE_DIR SFML/Graphics.hpp
        PATHS
        "C:/SFML/include"
        "C:/SFML-2.5.1/include"
        "C:/SFML-2.6.0/include"
        "C:/SFML-2.6.1/include"
    )
    find_library(SFML_GRAPHICS_LIB NAMES sfml-graphics sfml-graphics-s sfml-graphics-d
        PATHS
        "C:/SFML/lib"
        "C:/SFML-2.5.1/lib"
        "C:/SFML-2.6.0/lib"
        "C:/SFML-2.6.1/lib"
    )
    find_library(SFML_WINDOW_LIB NAMES sfml-window sfml-window-s sfml-window-d
        PATHS
        "C:/SFML/lib"
        "C:/SFML-2.5.1/lib"
        "C:/SFML-2.6.0/lib"
        "C:/SFML-2.6.1/lib"
    )
    find_library(SFML_SYSTEM_LIB NAMES sfml-system sfml-system-s sfml-system-d
        PATHS
        "C:/SFML/lib"
        "C:/SFML-2.5.1/lib"
        "C:/SFML-2.6.0/lib"
        "C:/SFML-2.6.1/lib"
    )

    if(SFML_INCLUDE_DIR AND SFML_GRAPHICS_LIB)
        message(STATUS "SFML 수동 지정 성공: ${SFML_INCLUDE_DIR}")
        target_include_directories(rl_constrained_fov PRIVATE ${SFML_INCLUDE_DIR})
        target_link_libraries(rl_constrained_fov PRIVATE
            ${SFML_GRAPHICS_LIB}
            ${SFML_WINDOW_LIB}
            ${SFML_SYSTEM_LIB}
        )
    else()
        message(FATAL_ERROR "SFML 라이브러리를 찾을 수 없습니다. C:/SFML 경로에 설치하거나 -DSFML_DIR을 설정하세요.")
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# OpenCV 라이브러리 탐색 및 설정 (조건부)
# ─────────────────────────────────────────────────────────────────────────────
if(USE_OPENCV)
    # cmake -DOpenCV_DIR=C:/opencv/build
    find_package(OpenCV QUIET)
    
    if(OpenCV_FOUND)
        message(STATUS "OpenCV Found: ${OpenCV_VERSION}")
        target_compile_definitions(rl_constrained_fov PRIVATE USE_OPENCV)
        target_include_directories(rl_constrained_fov PRIVATE ${OpenCV_INCLUDE_DIRS})
        target_link_libraries(rl_constrained_fov PRIVATE ${OpenCV_LIBS})
    else()
        message(WARNING "OpenCV가 시스템에서 발견되지 않았습니다. OpenCV 기능(VideoWriter) 없이 빌드합니다.")
        message(STATUS "OpenCV와 함께 빌드하려면 cmake -DOpenCV_DIR=<OpenCV_BUILD_DIR> 옵션을 사용하세요.")
    endif()
endif()

# Output 디렉토리 설정
set_target_properties(rl_constrained_fov PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
