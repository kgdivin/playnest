cmake_minimum_required(VERSION 3.21)
project(Playnest VERSION 3.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# Ressources compressées en zlib : compatible avec un Qt compilé sans zstd (build Windows statique).
set(CMAKE_AUTORCC_OPTIONS "--no-zstd")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)

set(SOURCES
    src/main.cpp
    src/core/Paths.cpp src/core/Paths.h
    src/core/Registry.cpp src/core/Registry.h
    src/core/Settings.cpp src/core/Settings.h
    src/core/RomDetector.cpp src/core/RomDetector.h
    src/core/Library.cpp src/core/Library.h
    src/core/Scanner.cpp src/core/Scanner.h
    src/core/Launcher.cpp src/core/Launcher.h
    src/core/AppInfo.h
    src/core/Lang.cpp src/core/Lang.h
    src/core/Net.cpp src/core/Net.h
    src/core/Archive.cpp src/core/Archive.h
    src/core/Tools.cpp src/core/Tools.h
    src/core/Emulators.cpp src/core/Emulators.h
    src/core/Saves.cpp src/core/Saves.h
    src/core/Organizer.cpp src/core/Organizer.h
    src/core/SelfUpdate.cpp src/core/SelfUpdate.h
    src/core/CoverManager.cpp src/core/CoverManager.h
    src/core/Gamepad.cpp src/core/Gamepad.h
    src/ui/Theme.cpp src/ui/Theme.h
    src/ui/GameDelegate.cpp src/ui/GameDelegate.h
    src/ui/Sidebar.cpp src/ui/Sidebar.h
    src/ui/HeroBand.cpp src/ui/HeroBand.h
    src/ui/HomePage.cpp src/ui/HomePage.h
    src/ui/OrganizeDialog.cpp src/ui/OrganizeDialog.h
    src/ui/WelcomeDialog.cpp src/ui/WelcomeDialog.h
    src/ui/DetailsPanel.cpp src/ui/DetailsPanel.h
    src/ui/EmulatorsPage.cpp src/ui/EmulatorsPage.h
    src/ui/SavesPage.cpp src/ui/SavesPage.h
    src/ui/SettingsPage.cpp src/ui/SettingsPage.h
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    resources/playnest.qrc
)
if(WIN32)
    list(APPEND SOURCES resources/playnest.rc)
endif()

qt_add_executable(Playnest WIN32 ${SOURCES})
target_compile_definitions(Playnest PRIVATE PLAYNEST_VERSION="${PROJECT_VERSION}")
target_link_libraries(Playnest PRIVATE Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Widgets Qt6::Network Qt6::Concurrent)
if(WIN32)
    target_link_libraries(Playnest PRIVATE version)
endif()

# Navigation manette : XInput sous Windows (chargé dynamiquement), SDL2 ailleurs si présent.
if(NOT WIN32)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        target_compile_definitions(Playnest PRIVATE PLAYNEST_HAVE_SDL2)
        if(TARGET SDL2::SDL2)
            target_link_libraries(Playnest PRIVATE SDL2::SDL2)
        else()
            target_include_directories(Playnest PRIVATE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(Playnest PRIVATE ${SDL2_LIBRARIES})
        endif()
        message(STATUS "Playnest : manette via SDL2")
    endif()
endif()

if(MINGW)
    # Exécutable autonome : runtime C++ lié statiquement.
    target_link_options(Playnest PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(MSVC)
    target_compile_options(Playnest PRIVATE /utf-8 /W3)
else()
    target_compile_options(Playnest PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

set_target_properties(Playnest PROPERTIES OUTPUT_NAME "Playnest")

# Tests de détection (exécutable console, sans interface).
option(PLAYNEST_BUILD_TESTS "Construire les tests de détection" OFF)
if(PLAYNEST_BUILD_TESTS)
    qt_add_executable(playnest_detect_test
        tests/detect_test.cpp
        src/core/Paths.cpp src/core/Lang.cpp src/core/Registry.cpp src/core/Settings.cpp src/core/RomDetector.cpp
        src/core/Library.cpp src/core/CoverManager.cpp src/core/Net.cpp src/core/Launcher.cpp
        src/core/Emulators.cpp src/core/Archive.cpp src/core/Tools.cpp src/core/Saves.cpp src/core/Organizer.cpp
        resources/playnest.qrc)
    target_link_libraries(playnest_detect_test PRIVATE Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Network Qt6::Concurrent)
    if(WIN32)
        target_link_libraries(playnest_detect_test PRIVATE version)
    endif()
    if(MINGW)
        target_link_options(playnest_detect_test PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    target_include_directories(playnest_detect_test PRIVATE src)
endif()

install(TARGETS Playnest RUNTIME DESTINATION .)
