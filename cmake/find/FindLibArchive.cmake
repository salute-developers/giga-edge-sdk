# A replacement for the FindLibArchive built into CMake: that one does not look for headers in include/archive (where
# the libarchive fork used by the SDK installs them), picks the shared library when both are present, and does not
# know the dependencies of a static build.
#
# Imported target: LibArchive::LibArchive (the same name as in the built-in module).
# Variables: LibArchive_FOUND, LibArchive_VERSION, LibArchive_INCLUDE_DIRS, LibArchive_LIBRARIES.
#
# An external libarchive can be given with the standard LibArchive_LIBRARY/LibArchive_INCLUDE_DIR or with
# LIBARCHIVE_LIBRARY/LIBARCHIVE_SOURCE_DIR (the directory with archive.h), the names some parent builds pass.

if(LIBARCHIVE_LIBRARY AND NOT LibArchive_LIBRARY)
  set(LibArchive_LIBRARY "${LIBARCHIVE_LIBRARY}" CACHE FILEPATH "libarchive library")
endif()
if(LIBARCHIVE_SOURCE_DIR AND NOT LibArchive_INCLUDE_DIR)
  set(LibArchive_INCLUDE_DIR "${LIBARCHIVE_SOURCE_DIR}" CACHE PATH "libarchive include directory")
endif()

find_path(LibArchive_INCLUDE_DIR NAMES archive.h PATH_SUFFIXES archive)

# The static library goes first: the SDK embeds its dependencies into its artifacts.
if(WIN32)
  set(_libarchive_names archive_static archive libarchive)
else()
  set(_libarchive_names ${CMAKE_STATIC_LIBRARY_PREFIX}archive${CMAKE_STATIC_LIBRARY_SUFFIX} archive)
endif()
find_library(LibArchive_LIBRARY NAMES ${_libarchive_names})
unset(_libarchive_names)

if(LibArchive_INCLUDE_DIR AND EXISTS "${LibArchive_INCLUDE_DIR}/archive.h")
  file(STRINGS "${LibArchive_INCLUDE_DIR}/archive.h" _libarchive_version
    REGEX "^#define[ \t]+ARCHIVE_VERSION_ONLY_STRING[ \t]+\"[^\"]*\"$")
  string(REGEX REPLACE "^.*\"([^\"]*)\".*$" "\\1" LibArchive_VERSION "${_libarchive_version}")
  unset(_libarchive_version)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibArchive
  REQUIRED_VARS LibArchive_LIBRARY LibArchive_INCLUDE_DIR
  VERSION_VAR LibArchive_VERSION)

if(LibArchive_FOUND)
  set(LibArchive_INCLUDE_DIRS "${LibArchive_INCLUDE_DIR}")
  set(LibArchive_LIBRARIES "${LibArchive_LIBRARY}")

  if(NOT TARGET LibArchive::LibArchive)
    add_library(LibArchive::LibArchive UNKNOWN IMPORTED)
    set_target_properties(LibArchive::LibArchive PROPERTIES
      IMPORTED_LOCATION "${LibArchive_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LibArchive_INCLUDE_DIR}")

    get_filename_component(_libarchive_name "${LibArchive_LIBRARY}" NAME)
    if(_libarchive_name MATCHES "archive_static" OR
       (NOT WIN32 AND _libarchive_name MATCHES "\\${CMAKE_STATIC_LIBRARY_SUFFIX}$"))
      # A static libarchive needs the libraries it was built with. Whatever is found is linked: an extra one does no
      # harm, and a missing one breaks the link.
      set_property(TARGET LibArchive::LibArchive APPEND PROPERTY
        INTERFACE_COMPILE_DEFINITIONS LIBARCHIVE_STATIC)
      find_package(ZLIB QUIET)
      if(ZLIB_FOUND)
        set_property(TARGET LibArchive::LibArchive APPEND PROPERTY INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
      endif()
      find_package(Iconv QUIET)
      if(Iconv_FOUND AND NOT Iconv_IS_BUILT_IN)
        set_property(TARGET LibArchive::LibArchive APPEND PROPERTY INTERFACE_LINK_LIBRARIES Iconv::Iconv)
      endif()
      if(WIN32)
        set_property(TARGET LibArchive::LibArchive APPEND PROPERTY INTERFACE_LINK_LIBRARIES Bcrypt)
      endif()
    endif()
    unset(_libarchive_name)
  endif()
endif()

mark_as_advanced(LibArchive_INCLUDE_DIR LibArchive_LIBRARY)
