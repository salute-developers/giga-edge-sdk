# The library takes the targets that exist at its configuration: dsplib of audio, onnxruntime of model, yaml-cpp
# (tts may bring it); without them it brings its own copies or builds less. Its own copies of speexdsp and dsplib must
# stay after the copies of the other modules in the link line.
register_module(
  enhance AFTER audio model tts
)
