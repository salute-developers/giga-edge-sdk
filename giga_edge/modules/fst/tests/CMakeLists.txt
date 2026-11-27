cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  fst_tests
  SRCS
    src/fst_rule/fst_rule.cpp
    src/nodes/float_grammar.cpp
    src/nodes/float_grammar_by_key.cpp
    src/nodes/string_grammar.cpp
    src/nodes/string_grammar_all.cpp
  LIBS
    fst_module
    node_test_utils
  DATA data
)
