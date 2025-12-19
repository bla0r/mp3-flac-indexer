cmake_minimum_required(VERSION 3.16)
project(mp3flac_indexer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer TagLib's official CMake config (TagLib 2.x exports TagLib::tag)
find_package(TagLib CONFIG QUIET)
if(NOT TagLib_FOUND)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
  find_package(Taglib REQUIRED) # Provides Taglib_LIBRARIES / Taglib_INCLUDE_DIRS
endif()

add_executable(mp3flac-indexer
  src/main.cpp
)

if(TagLib_FOUND)
  target_link_libraries(mp3flac-indexer PRIVATE TagLib::tag)
else()
  target_include_directories(mp3flac-indexer PRIVATE ${Taglib_INCLUDE_DIRS})
  target_link_libraries(mp3flac-indexer PRIVATE ${Taglib_LIBRARIES})
endif()

if(MSVC)
  target_compile_options(mp3flac-indexer PRIVATE /W4)
else()
  target_compile_options(mp3flac-indexer PRIVATE -Wall -Wextra -Wpedantic)
endif()
