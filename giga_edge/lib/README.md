# Libraries

A library (`lib`) is a piece of the SDK's infrastructure and is always built. Its CMake target is `<name>_lib`.

## 1. Directory layout

```
giga_edge/lib/my_lib/
├── CMakeLists.txt
├── contrib/
│   └── CMakeLists.txt
├── include/giga_edge/lib/my_lib/
│   └── public_header.h
├── internal/giga_edge/lib/my_lib/
│   └── internal_header.h
├── src/
│   ├── public_impl.cpp
│   └── private_impl.cpp
└── tests/
```

## 2. `CMakeLists.txt` of a library

A library is built with one call of the `giga_edge_lib(...)` helper from `cmake/Lib.cmake`. The helper creates the
`<name>_lib` target and picks up `contrib/`, `include/`, `internal/` and `tests/` when they are there.

```cmake
cmake_minimum_required(VERSION 3.13)

giga_edge_lib(my_lib
  SRCS
    src/public_impl.cpp
    src/private_impl.cpp
  DEPS
    common_lib
    ...
  PRIVATE_DEPS
    ...
)
```
