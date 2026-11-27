cmake_minimum_required(VERSION 3.13)

# run_in_parallel.h uses std::thread.
find_package(Threads REQUIRED)

giga_edge_lib(common
  SRCS
    src/serialization.cpp
    src/str_utils.cpp
  DEPS
    Threads::Threads
)
