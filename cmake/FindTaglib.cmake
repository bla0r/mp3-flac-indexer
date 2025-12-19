# Minimal FindTaglib.cmake
# Defines:
#   Taglib_FOUND
#   Taglib_INCLUDE_DIRS
#   Taglib_LIBRARIES

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PC_TAGLIB QUIET taglib)
endif()

find_path(Taglib_INCLUDE_DIR
  NAMES taglib/taglib.h
  HINTS ${PC_TAGLIB_INCLUDEDIR} ${PC_TAGLIB_INCLUDE_DIRS}
)

find_library(Taglib_LIBRARY
  NAMES tag
  HINTS ${PC_TAGLIB_LIBDIR} ${PC_TAGLIB_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Taglib DEFAULT_MSG Taglib_LIBRARY Taglib_INCLUDE_DIR)

if(Taglib_FOUND)
  set(Taglib_LIBRARIES ${Taglib_LIBRARY})
  set(Taglib_INCLUDE_DIRS ${Taglib_INCLUDE_DIR})
endif()

mark_as_advanced(Taglib_LIBRARY Taglib_INCLUDE_DIR)
