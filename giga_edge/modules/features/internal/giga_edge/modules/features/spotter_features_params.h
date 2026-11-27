#pragma once

namespace giga_edge {

struct SpotterFeatureParams {
  // Frequency (Hz) of speech signal recording.
  int sampling_freq;

  // Frame size in wav samples.
  int frame_size_samples;

  // Frame step size in wav samples.
  int frame_stride_samples;

  // Number of frequencies in fft.
  int num_freq;

  // Number of filterbank channels.
  int num_mels;

  // Normalize gain to this db value.
  int ref_db;

  // Min gain in db.
  int min_db;

  // If true normalize features using rolling normalization.
  bool rolling_norm;
};

} // namespace giga_edge