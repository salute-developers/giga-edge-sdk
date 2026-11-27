cmake_minimum_required(VERSION 3.13)

set(FEATURES_CONTRIB_DIR ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(asr_features)
# Eigen for asr_features (its own build is left as is) comes from the package shared by the whole SDK. The signature
# without PUBLIC/PRIVATE matches its own target_link_libraries: CMake does not allow mixing them.
target_link_libraries(asr_features Eigen3::Eigen)
