#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/modules/model/interpreter/interpreter.h"

#include <cstddef>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace giga_edge::vits {

struct TensorSpec {
  std::string name;
  Tensor::DataType type;
  int rank;
};

// A model of the text stage: its ONNX session and its path for the messages.
struct TextModel {
  std::string path;
  InterpreterPtr session;
};

// The model is read with its directory at the root of the reader: weights in a file are looked for next to the model
// (<name>.onnx.data, as onnx saves them).
// `environment`: a shared ONNX environment of `environments`; empty: an own session of one thread. `in_place`: the
// model is in the ORT format and its weights are used from the file as the reader maps it, with no copy, no memory
// arena and no prepacking; throws std::invalid_argument naming `path` for a file of another format.
TextModel OpenTextModel(
  const std::string& path,
  const std::string& environment,
  const IDataReaderPtr& data_reader,
  const std::map<std::string, EnvironmentPtr>* environments,
  bool in_place = false
);

// Runs the model; an error of the run names the file of the model.
void RunTextModel(const TextModel& model);

// std::runtime_error "vits: model '<path>': <what>".
[[noreturn]] void ThrowTextModelError(const TextModel& model, const std::string& what);

// Throws std::runtime_error naming the path unless the model has these inputs and outputs of these types and ranks
// and no other input: the code writes and reads the tensors by them.
void CheckTextModel(
  const TextModel& model, const std::vector<TensorSpec>& inputs, const std::vector<TensorSpec>& outputs
);

// The vocabulary of a model carries one line that is not a token, "[positions]<TAB>N": the longest input of the
// model. Takes the line out of the text and returns N; throws std::runtime_error naming `name` without the line or
// with N below `least` or above kMaxPositions.
inline constexpr size_t kMaxPositions = size_t{1} << 20;
size_t TakePositions(std::string& vocabulary, const std::string& name, size_t least);

// The sizes of a tensor as the model module takes them; throws std::length_error unless every size and the number
// of elements fit: a narrowed size would resize the tensor short of what is written into it.
std::vector<int> TensorShape(std::initializer_list<size_t> sizes);

} // namespace giga_edge::vits
