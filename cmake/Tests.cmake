function(add_giga_edge_test NAME)

  cmake_parse_arguments(
      PARSE_ARGV 1
      ARGS
      ""
      "DATA"
      "SRCS;LIBS"
  )

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/main.cpp"
      "#define CATCH_CONFIG_RUNNER\n"
      "#include <catch.hpp>\n"
      "#include <clocale>\n"
      "\n"
      "int main(int argc, char* argv[]) {\n"
      "  if (!std::setlocale(LC_ALL, \"en_US.utf8\")) {\n"
      "    std::setlocale(LC_ALL, \"en_US.UTF-8\");\n"
      "  }\n"
      "  return Catch::Session().run(argc, argv);\n"
      "}\n"
  )

  add_executable(
    ${NAME}
    ${CMAKE_CURRENT_BINARY_DIR}/main.cpp
    ${ARGS_SRCS}
  )
  target_link_libraries(${NAME} Catch2::Catch2 ${ARGS_LIBS})

  include(Catch)
  catch_discover_tests(${NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

  if(ARGS_DATA)
    # The data directory: a relative one is resolved from the test directory, an absolute one (data of a neighboring
    # module) is copied under its own name.
    set(_data_source "${CMAKE_CURRENT_SOURCE_DIR}/${ARGS_DATA}")
    set(_data_dest "${ARGS_DATA}")
    if(IS_ABSOLUTE "${ARGS_DATA}")
      string(REGEX REPLACE "/+$" "" _data_source "${ARGS_DATA}")
      get_filename_component(_data_dest "${_data_source}" NAME)
    endif()
    if(EXISTS "${_data_source}")
      add_custom_command(TARGET ${NAME} PRE_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${_data_source}/ ${CMAKE_CURRENT_BINARY_DIR}/${_data_dest})
    else()
      message(FATAL_ERROR "${NAME}: no test data directory ${_data_source}")
    endif()
  endif()
endfunction()

# DEFINE is the path to the executable of TOOL relative to the directory of the test NAME (its working directory, as
# for DATA): an absolute path would break the test in a build moved to another directory.
function(giga_edge_test_tool NAME TOOL DEFINE)
  get_target_property(test_dir ${NAME} BINARY_DIR)
  get_target_property(tool_dir ${TOOL} BINARY_DIR)
  file(RELATIVE_PATH tool_dir "${test_dir}" "${tool_dir}")
  string(REGEX REPLACE "/$" "" tool_dir "${tool_dir}")
  if(NOT tool_dir)
    set(tool_dir .)
  endif()
  target_compile_definitions(${NAME} PRIVATE ${DEFINE}="${tool_dir}/$<TARGET_FILE_NAME:${TOOL}>")
  add_dependencies(${NAME} ${TOOL})
endfunction()

# DEFINE is the path to the source tree relative to the directory of the test NAME (its working directory): the test
# reads files of the repository that are not its own data.
function(giga_edge_test_source_dir NAME DEFINE)
  get_target_property(test_dir ${NAME} BINARY_DIR)
  file(RELATIVE_PATH source_dir "${test_dir}" "${giga_edge_runtime_SOURCE_DIR}")
  target_compile_definitions(${NAME} PRIVATE ${DEFINE}="${source_dir}")
endfunction()
