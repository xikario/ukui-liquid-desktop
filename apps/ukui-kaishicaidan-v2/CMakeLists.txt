cmake_minimum_required(VERSION 3.16)
project(ukui-kaishicaidan-v2 VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_SKIP_BUILD_RPATH ON)

set(KAISHICAIDAN_QT_SDK_ROOT
    "${CMAKE_SOURCE_DIR}/.local-deps"
    CACHE PATH "Qt5 SDK root used when system development files are unavailable")

find_package(Qt5 QUIET COMPONENTS Core Gui Widgets DBus)
if(NOT Qt5_FOUND)
    list(PREPEND CMAKE_PREFIX_PATH
        "${KAISHICAIDAN_QT_SDK_ROOT}/root/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake")
    find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets DBus)
endif()

find_path(X11_INCLUDE_DIR X11/Xlib.h
    HINTS "${KAISHICAIDAN_QT_SDK_ROOT}/root/usr/include")
find_library(X11_LIBRARY
    NAMES X11 libX11.so.6
    HINTS
        "/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
if(NOT X11_INCLUDE_DIR OR NOT X11_LIBRARY)
    message(FATAL_ERROR "X11 development headers or runtime library were not found")
endif()

find_library(XTST_LIBRARY
    NAMES Xtst libXtst.so.6
    HINTS
        "/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
if(NOT XTST_LIBRARY)
    message(FATAL_ERROR "libXtst (X Record extension) was not found")
endif()

# Qt5's QOpenGLWidget headers include GL/gl.h. Some minimal Kylin SDKs ship
# the runtime libGL but omit the development header from the default include
# path, so allow an SDK-local header directory as a build-time hint.
find_path(KAISHICAIDAN_GL_INCLUDE_DIR GL/gl.h
    HINTS
        "${KAISHICAIDAN_GL_INCLUDE_DIR}"
        "/usr/include"
        "/usr/include/${CMAKE_LIBRARY_ARCHITECTURE}"
        "${KAISHICAIDAN_QT_SDK_ROOT}/root/usr/include")
if(NOT KAISHICAIDAN_GL_INCLUDE_DIR)
    message(FATAL_ERROR "GL/gl.h was not found; install OpenGL development headers or set KAISHICAIDAN_GL_INCLUDE_DIR")
endif()

add_executable(ukui-kaishicaidan-v2
    src/main.cpp
    src/StartMenu.cpp
    src/StartButton.cpp
    src/KeyInterceptor.cpp
    src/TaskbarDetector.cpp
    src/AppRegistry.cpp
    src/RecentFiles.cpp
    src/SettingsDialog.cpp
    src/NextKdeGlassView.cpp
    src/glass.qrc
)

target_compile_definitions(ukui-kaishicaidan-v2 PRIVATE
    UKUI_KAISHICAIDAN_V2=1
)

target_include_directories(ukui-kaishicaidan-v2 PRIVATE
    src/
    ${X11_INCLUDE_DIR}
    ${KAISHICAIDAN_GL_INCLUDE_DIR}
)

target_link_libraries(ukui-kaishicaidan-v2 PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::DBus
    ${X11_LIBRARY}
    ${XTST_LIBRARY}
)

# Build-only by default; installation is an explicit separate step.
option(BUILD_GLASS_TESTS "Build isolated glass rendering regression checks" OFF)
if(BUILD_GLASS_TESTS)
    add_executable(glass-render-test tests/glass-render-test.cpp
        src/NextKdeGlassView.cpp src/glass.qrc)
    target_include_directories(glass-render-test PRIVATE src ${KAISHICAIDAN_GL_INCLUDE_DIR})
    target_link_libraries(glass-render-test PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
endif()

# One shared implementation for launcher, fences, Smart Space and monitor.
set(UKUI_LIQUID_POPUP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/liquid-popup" CACHE PATH "Shared liquid popup source")
add_subdirectory("${UKUI_LIQUID_POPUP_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/liquid-popup")
target_link_libraries(ukui-kaishicaidan-v2 PRIVATE ukui-liquid-popup)

install(TARGETS ukui-kaishicaidan-v2 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS packaging/ukui-kaishicaidan-launcher DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/ukui-kaishicaidan.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES vendor/nextkde-glass/LICENSE vendor/nextkde-glass/README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/nextkde-glass)

include(CTest)
if(BUILD_TESTING)
    add_executable(launcher-metadata-test tests/metadata_test.cpp src/RecentFiles.cpp)
    target_include_directories(launcher-metadata-test PRIVATE src ${X11_INCLUDE_DIR} ${KAISHICAIDAN_GL_INCLUDE_DIR})
    target_link_libraries(launcher-metadata-test PRIVATE Qt5::Core Qt5::Gui ${X11_LIBRARY})
    add_test(NAME launcher-metadata COMMAND launcher-metadata-test)
endif()
