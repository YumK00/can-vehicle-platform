cmake_minimum_required(VERSION 3.19)   # 提升至与老师一致
project(vehicle_gateway VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Qt（允许 Qt5 或 Qt6）
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Svg SvgWidgets Network SerialBus)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Svg SvgWidgets Network SerialBus)

# ★ 关键：强制所有代码（包括 qmqtt 库自身）按静态方式处理符号
add_definitions(-DQMQTT_STATIC)

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
    res.qrc
)

add_executable(vehicle_gateway ${PROJECT_SOURCES}
    baseecu.h baseecu.cpp
    bmsecu.h bmsecu.cpp
    vehicledata.h
    vcuecu.h vcuecu.cpp
    bcmecu.h bcmecu.cpp
    dtcecu.h dtcecu.cpp
    mqttclient.h mqttclient.cpp
    gatewayclient.h gatewayclient.cpp
)

# 链接库（全部使用变量，兼容 Qt5/Qt6）
target_link_libraries(vehicle_gateway PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Svg
    Qt${QT_VERSION_MAJOR}::SvgWidgets
    Qt${QT_VERSION_MAJOR}::Network      # 显式添加，qmqtt 依赖
    Qt${QT_VERSION_MAJOR}::SerialBus    # 使用变量，不要硬编码 Qt6
    qmqtt
)

# 为主程序添加宏（与老师保持一致）
target_compile_definitions(vehicle_gateway PRIVATE MQTT_PROJECT_INCLUDE_SRC)

# 桌面属性（macOS 应用包 / Windows GUI 程序）
set_target_properties(vehicle_gateway PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS vehicle_gateway
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
