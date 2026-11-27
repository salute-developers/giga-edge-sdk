# OpenFst (https://www.openfst.org): it is installed only by an autotools build and has no CMake package; the install
# layout for CMake is defined by tools/dependencies/packages/openfst.
#
# Imported target: OpenFst::fst.
# Variables: OpenFst_FOUND, OpenFst_INCLUDE_DIR, OpenFst_LIBRARY.

find_path(OpenFst_INCLUDE_DIR NAMES fst/fst.h)
find_library(OpenFst_LIBRARY NAMES fst)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenFst REQUIRED_VARS OpenFst_LIBRARY OpenFst_INCLUDE_DIR)

if(OpenFst_FOUND AND NOT TARGET OpenFst::fst)
  add_library(OpenFst::fst UNKNOWN IMPORTED)
  set_target_properties(OpenFst::fst PROPERTIES
    IMPORTED_LOCATION "${OpenFst_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${OpenFst_INCLUDE_DIR}"
    # The openfst headers are noisy with warnings: SYSTEM, as in its own build.
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${OpenFst_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS}")
  if(WIN32)
    # Windows has no dlopen registration; the headers must see what the package was built with.
    set_property(TARGET OpenFst::fst PROPERTY INTERFACE_COMPILE_DEFINITIONS FST_NO_DYNAMIC_LINKING)
  endif()
endif()

mark_as_advanced(OpenFst_INCLUDE_DIR OpenFst_LIBRARY)
