# A synthesis backend may extend the module registration with its own src/interpreter/backends/<name>/module.cmake.
set(TTS_MODULE_ARGS DEPENDS model)
file(GLOB _tts_backend_modules "${CMAKE_CURRENT_LIST_DIR}/src/interpreter/backends/*/module.cmake")
foreach(_tts_backend_module IN LISTS _tts_backend_modules)
  include("${_tts_backend_module}")
endforeach()
register_module(tts ${TTS_MODULE_ARGS})
