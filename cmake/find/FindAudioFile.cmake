# Header-only AudioFile (https://github.com/adamstark/AudioFile): it does not install a CMake package of its own.
#
# Imported target: AudioFile::AudioFile.
# Variables: AudioFile_FOUND, AudioFile_INCLUDE_DIR.

# On macOS AudioToolbox.framework has a header with the same name, so frameworks are not searched.
set(_AudioFile_find_framework "${CMAKE_FIND_FRAMEWORK}")
set(CMAKE_FIND_FRAMEWORK NEVER)
find_path(AudioFile_INCLUDE_DIR NAMES AudioFile.h)
set(CMAKE_FIND_FRAMEWORK "${_AudioFile_find_framework}")
unset(_AudioFile_find_framework)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AudioFile REQUIRED_VARS AudioFile_INCLUDE_DIR)

if(AudioFile_FOUND AND NOT TARGET AudioFile::AudioFile)
  add_library(AudioFile::AudioFile INTERFACE IMPORTED)
  set_target_properties(AudioFile::AudioFile PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${AudioFile_INCLUDE_DIR}")
  if(MSVC)
    # M_PI from <cmath>.
    set_target_properties(AudioFile::AudioFile PROPERTIES INTERFACE_COMPILE_DEFINITIONS _USE_MATH_DEFINES)
  endif()
endif()

mark_as_advanced(AudioFile_INCLUDE_DIR)
