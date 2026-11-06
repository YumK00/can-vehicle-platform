cmake_minimum_required(VERSION 3.19)   # 提升至与老师一致

project(myphone VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

# QMQTT 支持（静态库）
set(QMQTT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qmqtt")
if(NOT EXISTS "${QMQTT_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "未找到 QMQTT 源码：${QMQTT_SOURCE_DIR}")
endif()
set(qmqtt_SHARED OFF CACHE BOOL "Build QMQTT as a static library" FORCE)
add_subdirectory("${QMQTT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/qmqtt-build")

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    gatewaypanel.cpp
    gatewaypanel.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(myphone
        MANUAL_FINALIZATION
        WIN32 MACOSX_BUNDLE                     # 添加老师样式的平台标志
        ${PROJECT_SOURCES}
        res.qrc
        cmdData.h
    )
else()
    if(ANDROID)
        add_library(myphone SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(myphone
            WIN32 MACOSX_BUNDLE                 # 非Qt6分支也加上
            ${PROJECT_SOURCES}
        )
    endif()
endif()


target_compile_definitions(myphone PRIVATE MQTT_PROJECT_INCLUDE_SRC QMQTT_STATIC)

target_link_libraries(myphone PRIVATE Qt${QT_VERSION_MAJOR}::Widgets qmqtt)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.myphone)
endif()
set_target_properties(myphone PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS myphone
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(myphone)
endif()
