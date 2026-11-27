cmake_minimum_required(VERSION 3.13)

set(SRC
    src/data_reader_helpers.cpp
    src/data_reader_factories.cpp
    src/directory_reader.cpp
    src/file_path.cpp
    src/lazy_caching_data_reader.cpp
    src/memory_mapped_directory_reader.cpp
    src/memory_mapped_file.cpp
)

if(ARCHIVE_SUPPORT)
  set(SRC ${SRC}
    src/archive_common.cpp
    src/archive_reader.cpp
    src/file_archive_common.cpp
    src/in_memory_tar_reader.cpp
    src/memory_mapped_tar_reader.cpp
  )
else()
  set(SRC ${SRC}
    src/empty_archive_reader.cpp
  )
endif()

find_package(Threads REQUIRED)

set(DEPS
  common_lib
  Threads::Threads
)

if(ARCHIVE_SUPPORT)
  list(APPEND DEPS LibArchive::LibArchive)
endif()

giga_edge_lib(data_reader
  SRCS ${SRC}
  PRIVATE_DEPS ${DEPS}
)
