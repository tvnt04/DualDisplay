cmake_minimum_required(VERSION 3.14)
project(DualDisplay VERSION 1.4.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Qt5 ──────────────────────────────────────────────────────────────────
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)

# ── Stub mode (no hardware needed) ───────────────────────────────────────
option(USING_STUB "Build without real KYFGLib hardware" OFF)

if(NOT USING_STUB)
    set(KAYA_ROOT "/opt/KAYA_Instruments" CACHE PATH "KAYA install root")
    find_path(KYFGLIB_INCLUDE_DIR NAMES KYFGLibA.h KYFGLib.h
        HINTS "${KAYA_ROOT}/include"
              "${KAYA_ROOT}/Examples/Vision Point API/include" REQUIRED)
    find_library(KYFGLIB_LIBRARY NAMES KYFGLibA KYFGLibA64 KYFGLib KYFGLib64
        HINTS "${KAYA_ROOT}/lib" "${KAYA_ROOT}/lib64" REQUIRED)
    find_library(KYVP_IMAGE_PROCESSING_LIBRARY NAMES KYVPImageProcessing
        HINTS "${KAYA_ROOT}/lib" "${KAYA_ROOT}/lib64" REQUIRED)
    message(STATUS "KYFGLib include : ${KYFGLIB_INCLUDE_DIR}")
    message(STATUS "KYFGLib library : ${KYFGLIB_LIBRARY}")
    message(STATUS "KYVP image processing library : ${KYVP_IMAGE_PROCESSING_LIBRARY}")
endif()

# ── Sources ───────────────────────────────────────────────────────────────
set(SOURCES
    main.cpp
    CameraWorker.h
    CameraWorker.cpp
    CameraPanel.h
    CameraPanel.cpp
    DetachedView.h
    MergeSaver.h
    MergeSaver.cpp
    AnalysisPanelWidget.h
    AnalysisPanelWidget.cpp
    MainWindow.h
    MainWindow.cpp
)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/OfflineViewer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/OfflineViewer.h"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/OfflineViewer.cpp")
    message(STATUS "OfflineViewer files found. Adding Offline Analyzer module.")
    list(APPEND SOURCES OfflineViewer.h OfflineViewer.cpp)
    add_compile_definitions(HAS_OFFLINE_VIEWER)
else()
    message(STATUS "OfflineViewer files not found. Compiling without Offline Analyzer.")
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<$<NOT:$<BOOL:${USING_STUB}>>:${KYFGLIB_INCLUDE_DIR}>
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    $<$<NOT:$<BOOL:${USING_STUB}>>:${KYFGLIB_LIBRARY}>
    $<$<NOT:$<BOOL:${USING_STUB}>>:${KYVP_IMAGE_PROCESSING_LIBRARY}>
    OpenMP::OpenMP_CXX
)

if(NOT USING_STUB)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "${KAYA_ROOT}/lib"
    )
endif()

find_package(OpenMP REQUIRED)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -O3 -march=native)

target_compile_definitions(${PROJECT_NAME} PRIVATE USING_STUB=$<BOOL:${USING_STUB}>)

message(STATUS "Stub mode : ${USING_STUB}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
