cmake_minimum_required(VERSION 3.16)
project(BibleStudyStudio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SWORD REQUIRED sword)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(bible_study_studio
    src/main.cpp
    src/SwordEngine.h
    src/SwordEngine.cpp
    src/GeminiClient.h
    src/GeminiClient.cpp
    src/ScreenPanels.h
    src/ScreenPanels.cpp
    src/ModuleInstallerDialog.h
    src/ModuleInstallerDialog.cpp
    src/LayoutSelectorDialog.h
    src/LayoutSelectorDialog.cpp
    src/MainWindow.h
    src/MainWindow.cpp
)

target_include_directories(bible_study_studio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SWORD_INCLUDE_DIRS}
)

target_link_libraries(bible_study_studio PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    ${SWORD_LIBRARIES}
)

if(WIN32)
    set_target_properties(bible_study_studio PROPERTIES
        WIN32_EXECUTABLE TRUE
        OUTPUT_NAME "AI_Bible"
    )
endif()

