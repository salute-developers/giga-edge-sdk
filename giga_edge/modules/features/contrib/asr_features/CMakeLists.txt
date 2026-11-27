cmake_minimum_required(VERSION 3.13)

add_library(asr_features STATIC
    dither.cc
    feature_params.cc
    fft_calculator.cc
    melspec_extractor.cc
    mfsc.cc
    power_spectrum.cc
    pre_emphasis.cc
    speech_utils.cc
    tri_filterbank.cc
    windowing.cc
)

target_include_directories(asr_features PUBLIC ${FEATURES_CONTRIB_DIR})
target_link_libraries(asr_features kissfft)
target_compile_definitions(asr_features PUBLIC EIGEN_MPL2_ONLY)
