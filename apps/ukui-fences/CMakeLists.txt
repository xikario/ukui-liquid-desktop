cmake_minimum_required(VERSION 3.16)
project(ukui-fences VERSION 0.4.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_SKIP_BUILD_RPATH ON)

set(UKUI_FENCES_QT_SDK_ROOT
    "${CMAKE_SOURCE_DIR}/.local-deps"
    CACHE PATH "Qt5 SDK root used when system development files are unavailable")

find_package(Qt5 QUIET COMPONENTS Core Gui Widgets DBus Network)
if(NOT Qt5_FOUND)
    list(PREPEND CMAKE_PREFIX_PATH
        "${UKUI_FENCES_QT_SDK_ROOT}/root/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake")
    find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets DBus Network)
endif()
find_package(ZLIB REQUIRED)

find_path(UKUI_FENCES_GL_INCLUDE_DIR GL/gl.h
    HINTS "${UKUI_FENCES_QT_SDK_ROOT}/root/usr/include")
if(NOT UKUI_FENCES_GL_INCLUDE_DIR)
    message(FATAL_ERROR "GL/gl.h missing; install OpenGL development headers or set UKUI_FENCES_GL_INCLUDE_DIR")
endif()

find_path(UKUI_FENCES_X11_INCLUDE_DIR X11/Xlib.h
    HINTS "${UKUI_FENCES_QT_SDK_ROOT}/root/usr/include")
find_library(UKUI_FENCES_X11_LIBRARY
    NAMES X11 libX11.so.6
    HINTS
        "/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
if(NOT UKUI_FENCES_X11_INCLUDE_DIR OR NOT UKUI_FENCES_X11_LIBRARY)
    message(FATAL_ERROR "X11 development headers or runtime library were not found")
endif()

add_executable(ukui-fences
    src/main.cpp
    src/DesktopCanvas.cpp
    src/DesktopWidgets.cpp
    src/StrawberryPlayer.cpp
    src/MusicDesklet.cpp
    src/CalendarDesklet.cpp
    src/DeskletModels.cpp
    src/ActivityRecorder.cpp
    src/FenceWidget.cpp
    src/FenceGlassRenderer.cpp
    src/fence-glass.qrc
    src/DesktopIcon.cpp
    src/FileClipboard.cpp
    src/SystemMonitor.cpp
    src/SmartSpaceWidget.cpp
    src/MenuStyle.h
    resources.qrc
)

target_compile_definitions(ukui-fences PRIVATE
    UKUI_FENCES_CREDENTIAL_STORE_PATH="${CMAKE_SOURCE_DIR}/scripts/credential_store.py"
    UKUI_FENCES_CALENDAR_READER_PATH="${CMAKE_SOURCE_DIR}/scripts/calendar_snapshot.py"
    UKUI_FENCES_SMART_INDEXER_PATH="${CMAKE_SOURCE_DIR}/scripts/smart_space_indexer.py"
    UKUI_FENCES_KNOWLEDGE_WORKER_PATH="${CMAKE_SOURCE_DIR}/scripts/smart_space_knowledge.py")

target_include_directories(ukui-fences PRIVATE
    src/
    ${UKUI_FENCES_X11_INCLUDE_DIR}
    ${UKUI_FENCES_GL_INCLUDE_DIR}
)

target_link_libraries(ukui-fences PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::DBus
    Qt5::Network
    ZLIB::ZLIB
    ${UKUI_FENCES_X11_LIBRARY}
)

if(BUILD_TESTING)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME smart_space_indexer
            COMMAND ${Python3_EXECUTABLE} -m unittest -v
                    tests/test_smart_space_indexer.py)
        set_tests_properties(smart_space_indexer PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180)
        add_test(NAME smart_space_knowledge
            COMMAND ${Python3_EXECUTABLE} -m unittest -v
                    tests/test_smart_space_knowledge.py)
        set_tests_properties(smart_space_knowledge PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180)
    endif()

    find_program(XVFB_RUN_EXECUTABLE xvfb-run)
    find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
    if(XVFB_RUN_EXECUTABLE AND DBUS_RUN_SESSION_EXECUTABLE)
        get_target_property(FENCE_TEST_SOURCES ukui-fences SOURCES)
        list(REMOVE_ITEM FENCE_TEST_SOURCES src/main.cpp)
        add_executable(fence-glass-test tests/fence_glass_test.cpp tests/music_fixture.h ${FENCE_TEST_SOURCES})
        target_include_directories(fence-glass-test PRIVATE src ${UKUI_FENCES_X11_INCLUDE_DIR} ${UKUI_FENCES_GL_INCLUDE_DIR})
        target_compile_definitions(fence-glass-test PRIVATE $<TARGET_PROPERTY:ukui-fences,COMPILE_DEFINITIONS>)
        target_link_libraries(fence-glass-test PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::DBus Qt5::Network ZLIB::ZLIB ${UKUI_FENCES_X11_LIBRARY})
        foreach(GLASS_BACKEND gpu cpu)
            add_test(NAME fence_glass_${GLASS_BACKEND}
                COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                        ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:fence-glass-test>
                        ${CMAKE_SOURCE_DIR}/test-results/fence-glass-${GLASS_BACKEND})
            set_tests_properties(fence_glass_${GLASS_BACKEND} PROPERTIES TIMEOUT 90 LABELS "integration;ui;glass")
        endforeach()
        set_tests_properties(fence_glass_cpu PROPERTIES ENVIRONMENT "UKUI_FENCES_GLASS_NO_GL=1")
        set_tests_properties(fence_glass_gpu PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME smart_space_wallpaper_sync
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:fence-glass-test>
                    --wallpaper-only)
        set_tests_properties(smart_space_wallpaper_sync PROPERTIES
            TIMEOUT 45 LABELS "integration;ui;glass;wallpaper")
        add_test(NAME desktop_selection_drag
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:fence-glass-test>
                    --selection-only)
        set_tests_properties(desktop_selection_drag PROPERTIES
            TIMEOUT 60 LABELS "integration;ui;selection"
            ENVIRONMENT "QT_STYLE_OVERRIDE=ukui;QT_SCREEN_SCALE_FACTORS=1")
        add_test(NAME desktop_selection_drag_scaled
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 2160x1350x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:fence-glass-test>
                    --selection-only)
        set_tests_properties(desktop_selection_drag_scaled PROPERTIES
            TIMEOUT 60 LABELS "integration;ui;selection"
            ENVIRONMENT "QT_STYLE_OVERRIDE=ukui;QT_SCREEN_SCALE_FACTORS=1;QT_SCALE_FACTOR=1.5")
        add_test(NAME fence_features
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:fence-glass-test>
                    --features-only)
        set_tests_properties(fence_features PROPERTIES
            TIMEOUT 30 LABELS "integration;ui;fences" ENVIRONMENT "UKUI_FENCES_GLASS_NO_GL=1")
        add_test(NAME desktop_screen_change
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 2880x1800x24 +extension RANDR"
                    ${DBUS_RUN_SESSION_EXECUTABLE} --
                    ${CMAKE_SOURCE_DIR}/tests/run_screen_change_smoke.sh
                    $<TARGET_FILE:ukui-fences>)
        set_tests_properties(desktop_screen_change PROPERTIES
            TIMEOUT 30 LABELS "integration;ui;screen")
        add_test(NAME smart_space_ui_smoke
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} --
                    ${CMAKE_SOURCE_DIR}/tests/run_smart_space_ui_smoke.sh
                    $<TARGET_FILE:ukui-fences>)
        set_tests_properties(smart_space_ui_smoke PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 120
            LABELS "integration;ui")
        add_test(NAME smart_space_policy_smoke
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} --
                    ${CMAKE_SOURCE_DIR}/tests/run_smart_space_policy_smoke.sh
                    $<TARGET_FILE:ukui-fences>)
        set_tests_properties(smart_space_policy_smoke PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
            LABELS "integration;ui;policy")
        add_test(NAME smart_space_new_features_smoke
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} --
                    ${CMAKE_SOURCE_DIR}/tests/run_smart_space_new_features_smoke.sh
                    $<TARGET_FILE:ukui-fences>)
        set_tests_properties(smart_space_new_features_smoke PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 90
            LABELS "integration;ui;theme")
        add_test(NAME smart_space_glass_smoke
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} --
                    ${CMAKE_SOURCE_DIR}/tests/run_smart_space_glass_smoke.sh
                    $<TARGET_FILE:ukui-fences>)
        set_tests_properties(smart_space_glass_smoke PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
            LABELS "integration;ui;theme;glass")
        add_test(NAME smart_space_resource_benchmark
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} --
                    ${CMAKE_SOURCE_DIR}/tests/run_smart_space_resource_benchmark.sh
                    $<TARGET_FILE:ukui-fences>)
        set_tests_properties(smart_space_resource_benchmark PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180
            LABELS "integration;resource")
        add_test(NAME smart_space_dbus_dialogs
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                    ${DBUS_RUN_SESSION_EXECUTABLE} --
                    ${CMAKE_SOURCE_DIR}/tests/run_smart_space_dbus_dialog_smoke.sh
                    $<TARGET_FILE:ukui-fences>)
        set_tests_properties(smart_space_dbus_dialogs PROPERTIES TIMEOUT 30 LABELS "integration;dbus")
        find_program(GNOME_KEYRING_EXECUTABLE gnome-keyring-daemon)
        if(GNOME_KEYRING_EXECUTABLE AND Python3_Interpreter_FOUND)
            add_test(NAME credential_store
                COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 1440x900x24"
                        ${DBUS_RUN_SESSION_EXECUTABLE} -- ${Python3_EXECUTABLE}
                        ${CMAKE_SOURCE_DIR}/tests/test_credential_store.py
                        $<TARGET_FILE:ukui-fences>)
            set_tests_properties(credential_store PROPERTIES TIMEOUT 45 LABELS "integration;credentials")
        endif()
    endif()
endif()

install(TARGETS ukui-fences RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS packaging/ukui-fences-launcher DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS scripts/smart_space_indexer.py
                 scripts/credential_store.py
                 scripts/smart_space_knowledge.py
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/ukui-fences)
install(FILES packaging/ukui-fences.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/ukui-fences.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES packaging/ukui-fences.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(DIRECTORY assets/fence-icons/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/ukui-fences/fence-icons
        FILES_MATCHING PATTERN "*.svg")
install(FILES packaging/ukui-fences-autostart.desktop
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart)
install(FILES README.md docs/SMART_SPACE.md docs/SMART_SPACE_TEST_REPORT.md
              docs/SMART_SPACE_KNOWLEDGE.md docs/FENCE_LIQUID_GLASS.md docs/DESKTOP_MENU.md docs/DESKTOP_WIDGETS.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES vendor/nextkde-glass/LICENSE vendor/nextkde-glass/README.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/nextkde-glass)

# One shared implementation for launcher, fences, Smart Space and monitor.
set(UKUI_LIQUID_POPUP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/liquid-popup" CACHE PATH "Shared liquid popup source")
option(UKUI_FENCES_BUILD_POPUP_PREVIEW "Build and install the shared popup preview" ON)
if(UKUI_FENCES_BUILD_POPUP_PREVIEW)
    set(LIQUID_POPUP_BUILD_DEMO ON)
endif()
add_subdirectory("${UKUI_LIQUID_POPUP_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/liquid-popup")
target_link_libraries(ukui-fences PRIVATE ukui-liquid-popup)
if(TARGET fence-glass-test)
    target_link_libraries(fence-glass-test PRIVATE ukui-liquid-popup)
endif()

if(TARGET liquid-popup-preview)
    add_dependencies(ukui-fences liquid-popup-preview)
    target_compile_definitions(ukui-fences PRIVATE
    UKUI_FENCES_CALENDAR_READER_PATH="${CMAKE_SOURCE_DIR}/scripts/calendar_snapshot.py"
        UKUI_LIQUID_POPUP_PREVIEW_PATH="$<TARGET_FILE:liquid-popup-preview>")
    install(TARGETS liquid-popup-preview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# New desklets share the same optical renderer as the liquid panel.
set(UKUI_LIQUID_GL_INCLUDE_DIR "${UKUI_FENCES_GL_INCLUDE_DIR}")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/liquid-glass" "${CMAKE_CURRENT_BINARY_DIR}/liquid-glass")
target_link_libraries(ukui-fences PRIVATE ukui-liquid-optics)
if(TARGET fence-glass-test)
    target_link_libraries(fence-glass-test PRIVATE ukui-liquid-optics)
endif()
install(FILES vendor/nextkde-desklets/LICENSE vendor/nextkde-desklets/README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/nextkde-desklets)

if(TARGET fence-glass-test)
    foreach(DESKLET_SCALE 1 1.5 2)
        add_test(NAME desktop_desklets_${DESKLET_SCALE}
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 2880x1800x24"
                ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:fence-glass-test> --desklets-only)
        set_tests_properties(desktop_desklets_${DESKLET_SCALE} PROPERTIES
            TIMEOUT 90 ENVIRONMENT "QT_FONT_DPI=96;QT_SCALE_FACTOR=${DESKLET_SCALE};QT_SCREEN_SCALE_FACTORS=1;UKUI_LIQUID_GLASS_NO_GL=1"
            LABELS "integration;ui;desklets")
    endforeach()
endif()

if(TARGET fence-glass-test)
    foreach(MUSIC_SCALE 1 1.5 2)
        add_test(NAME desktop_music_${MUSIC_SCALE}
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 2880x1800x24"
                ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:fence-glass-test> --music-only)
        set_tests_properties(desktop_music_${MUSIC_SCALE} PROPERTIES
            TIMEOUT 60 ENVIRONMENT "QT_FONT_DPI=96;QT_SCALE_FACTOR=${MUSIC_SCALE};QT_SCREEN_SCALE_FACTORS=1;UKUI_LIQUID_GLASS_NO_GL=1"
            LABELS "integration;ui;music")
    endforeach()
endif()

install(PROGRAMS scripts/calendar_snapshot.py DESTINATION ${CMAKE_INSTALL_BINDIR})

if(TARGET fence-glass-test)
    foreach(CALENDAR_SCALE 1 1.5 2)
        add_test(NAME desktop_calendar_${CALENDAR_SCALE}
            COMMAND ${XVFB_RUN_EXECUTABLE} -a -s "-screen 0 2880x1800x24"
                ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:fence-glass-test> --calendar-only)
        set_tests_properties(desktop_calendar_${CALENDAR_SCALE} PROPERTIES
            TIMEOUT 60 ENVIRONMENT "QT_FONT_DPI=96;QT_SCALE_FACTOR=${CALENDAR_SCALE};QT_SCREEN_SCALE_FACTORS=1;UKUI_LIQUID_GLASS_NO_GL=1"
            LABELS "integration;ui;calendar")
    endforeach()
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND)
    add_test(NAME calendar_snapshot COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_calendar_snapshot.py)
endif()

install(FILES scripts/chinese_calendar.py scripts/china_holidays_2026.json DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES vendor/holiday-cn/LICENSE vendor/holiday-cn/README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/ukui-fences/holiday-cn)
