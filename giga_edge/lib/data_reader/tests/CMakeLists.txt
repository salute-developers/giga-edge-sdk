cmake_minimum_required(VERSION 3.13)

find_package(Threads REQUIRED)

set(DATA_READER_TEST_SRCS src/main.cpp src/data_reader.cpp)
list(APPEND DATA_READER_TEST_SRCS src/file_path.cpp)
list(APPEND DATA_READER_TEST_SRCS src/memory_mapped_file.cpp)
add_executable(data_reader_tests ${DATA_READER_TEST_SRCS})
target_include_directories(data_reader_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(data_reader_tests data_reader_lib common_lib Threads::Threads Catch2::Catch2)

include(Catch)
catch_discover_tests(data_reader_tests)

add_custom_command(TARGET data_reader_tests PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/ ${CMAKE_CURRENT_BINARY_DIR}/data)
