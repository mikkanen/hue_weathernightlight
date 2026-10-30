cmake_minimum_required(VERSION 3.15)
project(hue_weathernightlight VERSION 1.0.0 LANGUAGES CXX)

# Tulostetaan viestejä konfiguroinnin aikana
message(STATUS "--- Konfigurointi alkaa ---")
message(STATUS "Projektin nimi:    ${PROJECT_NAME}")
message(STATUS "Versio:           ${PROJECT_VERSION}")
message(STATUS "Lähdekoodikansio: ${PROJECT_SOURCE_DIR}")
message(STATUS "Käännöskansio:    ${PROJECT_BINARY_DIR}")
message(STATUS "--- Konfigurointi loppuu ---")

# Jos käännöstyyppiä ei ole annettu, asetetaan oletukseksi Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Valitse käännöstyyppi" FORCE)
endif()

message(STATUS "Käännöstyyppi: ${CMAKE_BUILD_TYPE}")

# Asetetaan C++ standardi
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Määritellään suoritettava tiedosto
add_executable(${PROJECT_NAME} 
    src/main.cpp
    src/CurlHttpHandler.cpp
    )

# Kerrotaan mistä include-tiedostot löytyvät
target_include_directories(${PROJECT_NAME} PUBLIC include)

# Tunnistetaan kääntäjä ja asetetaan parhaat varoitukset
if(MSVC)
    # Microsoft Visual C++
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Fedora/Linux vakio-kääntäjät
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(hueplusplus REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE hueplusplus)

# JSON
find_package(nlohmann_json REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

# libcurl (TÄRKEÄ)
find_package(CURL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

configure_file(src/config.json ${CMAKE_CURRENT_BINARY_DIR}/config.json COPYONLY)