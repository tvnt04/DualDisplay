cmake_minimum_required(VERSION 3.14)
project(XDT VERSION 1.4.1 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(OpenMP QUIET)

option(USING_STUB "Build without real KYFGLib hardware" OFF)
option(PORTABLE_LINUX_BUNDLE "Stage a relocatable Linux application directory" ON)

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

set(SOURCES
    main.cpp
    AppVersion.h
    CameraWorker.h CameraWorker.cpp AlignedMemory.h NativeFile.h
    CameraPanel.h CameraPanel.cpp
    DetachedView.h VideoViewLabel.h ThemeToggleSwitch.h
    MergeSaver.h MergeSaver.cpp
    AnalysisPanelWidget.h AnalysisPanelWidget.cpp
    ThemeManager.h ThemeManager.cpp
    SettingsManager.h SettingsManager.cpp
    CalibrationEngine.h CalibrationEngine.cpp
    PlaybackController.h PlaybackController.cpp
    CaptureController.h CaptureController.cpp
    CameraManager.h CameraManager.cpp
    MainWindow.h MainWindow.cpp
    resources.qrc
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/OfflineViewer.cpp")
    list(APPEND SOURCES OfflineViewer.h OfflineViewer.cpp)
    add_compile_definitions(HAS_OFFLINE_VIEWER)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME xdt)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    $<$<NOT:$<BOOL:${USING_STUB}>>:${KYFGLIB_INCLUDE_DIR}>
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core Qt5::Gui Qt5::Widgets
    $<$<NOT:$<BOOL:${USING_STUB}>>:${KYFGLIB_LIBRARY}>
    $<$<NOT:$<BOOL:${USING_STUB}>>:${KYVP_IMAGE_PROCESSING_LIBRARY}>
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "OpenMP found: enabling parallel frame processing")
else()
    message(STATUS "OpenMP not found: building with serial frame processing")
endif()

if(NOT USING_STUB)
    set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_RPATH "${KAYA_ROOT}/lib")
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE USING_STUB=$<BOOL:${USING_STUB}>)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(UNIX AND NOT APPLE AND PORTABLE_LINUX_BUNDLE)
    find_path(QT5_PLUGINS_DIR NAMES platforms/libqxcb.so
        HINTS "${Qt5Core_DIR}/../../../plugins"
              "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/qt5/plugins"
              "/usr/lib/qt5/plugins")
    if(NOT QT5_PLUGINS_DIR)
        message(FATAL_ERROR "Could not find Qt5 platform plugins (expected platforms/libqxcb.so)")
    endif()

    get_target_property(QT5_CORE_LIBRARY Qt5::Core IMPORTED_LOCATION_RELEASE)
    if(NOT QT5_CORE_LIBRARY)
        get_target_property(QT5_CORE_LIBRARY Qt5::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(QT5_LIBRARY_DIR "${QT5_CORE_LIBRARY}" DIRECTORY)
    file(GLOB QT5_RUNTIME_LIBS "${QT5_LIBRARY_DIR}/libQt5*.so*")

    set(PORTABLE_BUNDLE_DIR "${CMAKE_BINARY_DIR}/portable/${PROJECT_NAME}-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}")
    set(PORTABLE_BUNDLE_LIB_DIR "${PORTABLE_BUNDLE_DIR}/lib")
    set(PORTABLE_QT_DIR "${PORTABLE_BUNDLE_LIB_DIR}/qt5/plugins")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/AppRun.in"
        "${CMAKE_BINARY_DIR}/portable/AppRun" COPYONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/README-portable.txt.in"
        "${CMAKE_BINARY_DIR}/portable/README-portable.txt" @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/BundleKayaDependencies.cmake.in"
        "${CMAKE_BINARY_DIR}/portable/BundleKayaDependencies.cmake" @ONLY)

    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "${PORTABLE_BUNDLE_DIR}")
    install(FILES "${QT5_PLUGINS_DIR}/platforms/libqxcb.so"
        DESTINATION "${PORTABLE_QT_DIR}/platforms")
    install(FILES ${QT5_RUNTIME_LIBS} DESTINATION "${PORTABLE_BUNDLE_LIB_DIR}")
    find_library(OPENMP_RUNTIME_LIBRARY NAMES gomp)
    if(OPENMP_RUNTIME_LIBRARY)
        install(FILES "${OPENMP_RUNTIME_LIBRARY}" DESTINATION "${PORTABLE_BUNDLE_LIB_DIR}")
    endif()

    if(NOT USING_STUB)
        install(FILES "${KYFGLIB_LIBRARY}" "${KYVP_IMAGE_PROCESSING_LIBRARY}"
            DESTINATION "${PORTABLE_BUNDLE_LIB_DIR}/kaya")
        install(SCRIPT "${CMAKE_BINARY_DIR}/portable/BundleKayaDependencies.cmake")
    endif()

    install(FILES "${CMAKE_BINARY_DIR}/portable/AppRun"
        DESTINATION "${PORTABLE_BUNDLE_DIR}"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ui_preferences.ini"
        DESTINATION "${PORTABLE_BUNDLE_DIR}" OPTIONAL)
    install(FILES "${CMAKE_BINARY_DIR}/portable/README-portable.txt"
        DESTINATION "${PORTABLE_BUNDLE_DIR}")

    add_custom_target(portable_linux_bundle
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PORTABLE_BUNDLE_DIR}"
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
            --prefix "${CMAKE_BINARY_DIR}/portable/install"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating portable Linux application folder")
    add_custom_target(portable_linux_archive
        COMMAND "${CMAKE_COMMAND}" -E tar czf
            "${CMAKE_BINARY_DIR}/portable/${PROJECT_NAME}-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}.tar.gz"
            --format=gnutar "${PROJECT_NAME}-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}"
        DEPENDS portable_linux_bundle
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/portable"
        COMMENT "Creating portable Linux tarball")
    message(STATUS "Portable Linux bundle staging path: ${PORTABLE_BUNDLE_DIR}")
endif()

message(STATUS "Stub mode : ${USING_STUB}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
