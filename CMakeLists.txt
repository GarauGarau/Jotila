cmake_minimum_required(VERSION 3.20)
project(jotilla_linux C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED glib-2.0 gobject-2.0 gio-2.0)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(X11 REQUIRED x11)
pkg_check_modules(X11 x11)

include_directories(${GLIB2_INCLUDE_DIRS} ${GTK3_INCLUDE_DIRS} ${CAIRO_INCLUDE_DIRS})
link_directories(${GLIB2_LIBRARY_DIRS} ${GTK3_LIBRARY_DIRS} ${CAIRO_LIBRARY_DIRS})

set(JOTILLA_CORE
    src/linux/date.c
    src/linux/tags.c
    src/linux/store.c
    src/linux/palette.c
)

set(JOTILLA_GUI
    src/linux/main.c
    src/linux/rtf.c
    src/linux/platform.c
    src/linux/note_window.c
    src/linux/archive_window.c
    src/linux/calendar.c
    src/linux/palette_picker.c
)

add_executable(jotilla ${JOTILLA_CORE} ${JOTILLA_GUI})
target_include_directories(jotilla PRIVATE src/linux)
target_compile_options(jotilla PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations)
target_link_libraries(jotilla PRIVATE
    ${GTK3_LIBRARIES} ${GLIB2_LIBRARIES} ${CAIRO_LIBRARIES} ${X11_LIBRARIES} m)

install(TARGETS jotilla RUNTIME DESTINATION bin)
install(FILES packaging/jotilla.desktop DESTINATION share/applications)
install(FILES packaging/jotilla.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES packaging/org.jotilla.Jotilla.metainfo.xml DESTINATION share/metainfo)

enable_testing()

add_executable(test_core
    tests/test_core.c
    ${JOTILLA_CORE}
)
target_include_directories(test_core PRIVATE src/linux)
target_compile_options(test_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(test_core PRIVATE ${GLIB2_LIBRARIES} m)
add_test(NAME core COMMAND test_core)

add_executable(test_rtf tests/test_rtf.c src/linux/rtf.c)
target_include_directories(test_rtf PRIVATE src/linux)
target_compile_options(test_rtf PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(test_rtf PRIVATE ${GTK3_LIBRARIES} ${GLIB2_LIBRARIES})
add_test(NAME rtf COMMAND test_rtf)

add_executable(test_extended
    tests/test_extended.c
    ${JOTILLA_CORE}
    src/linux/rtf.c
)
target_include_directories(test_extended PRIVATE src/linux)
target_compile_options(test_extended PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(test_extended PRIVATE
    ${GTK3_LIBRARIES} ${GLIB2_LIBRARIES} ${CAIRO_LIBRARIES} ${X11_LIBRARIES} m)
add_test(NAME extended COMMAND test_extended)

if(X11_FOUND)
    add_executable(window_probe tests/window_probe.c)
    target_include_directories(window_probe PRIVATE ${X11_INCLUDE_DIRS})
    target_link_libraries(window_probe PRIVATE ${X11_LIBRARIES})
endif()

pkg_check_modules(XTST xtst)
if(XTST_FOUND)
    add_executable(xui tests/xui.c)
    target_include_directories(xui PRIVATE ${X11_INCLUDE_DIRS} ${XTST_INCLUDE_DIRS})
    target_link_libraries(xui PRIVATE ${X11_LIBRARIES} ${XTST_LIBRARIES})
endif()

if(X11_FOUND)
    add_test(NAME e2e_smoke COMMAND ${CMAKE_SOURCE_DIR}/tests/e2e_smoke.sh $<TARGET_FILE:jotilla> $<TARGET_FILE:window_probe>)
else()
    add_test(NAME e2e_smoke COMMAND ${CMAKE_SOURCE_DIR}/tests/e2e_smoke.sh $<TARGET_FILE:jotilla>)
endif()
