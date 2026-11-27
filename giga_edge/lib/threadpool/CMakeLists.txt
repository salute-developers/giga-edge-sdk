cmake_minimum_required(VERSION 3.13)

find_package(Threads REQUIRED)

giga_edge_lib(threadpool
  SRCS
    src/threadpool.cpp
  DEPS
    common_lib
    enum_lib
    Threads::Threads
)
