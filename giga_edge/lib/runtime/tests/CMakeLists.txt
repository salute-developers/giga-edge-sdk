cmake_minimum_required(VERSION 3.13)

add_library(
  runtime_test_utils
  STATIC
  lib/runtime_test_utils/src/audio.cpp
  lib/runtime_test_utils/src/activation_matchers.cpp
)

target_include_directories(runtime_test_utils PUBLIC lib/runtime_test_utils/include)
target_link_libraries(runtime_test_utils PRIVATE Catch2::Catch2)
target_link_libraries(runtime_test_utils PUBLIC runtime_lib audio_lib runtime_data_lib spotter_module)

# The tests run spotter graphs: they cannot be built without the spotter and events modules (presets without them).
module_enabled(spotter _runtime_tests_spotter)
module_enabled(events _runtime_tests_events)
if(_runtime_tests_spotter AND _runtime_tests_events)
  set(RUNTIME_TEST_SRCS
    src/config.cpp
    src/events.cpp
    src/statistics_tracker.cpp
    src/submodel_environments.cpp
    src/submodels_remove_version.cpp
    src/submodels.cpp
    src/version_management_threads.cpp
  )

  add_giga_edge_test(
    runtime_tests
    SRCS
      ${RUNTIME_TEST_SRCS}
    LIBS
      audio_lib
      runtime_lib
      data_reader_lib
      test_utils_lib
      events_test_utils
      runtime_test_utils
      giga_edge_runtime
  )
endif()
