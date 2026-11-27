#include "giga_edge/modules/tts/interpreter/backends/vits/text/text_model.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/directory_reader.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/table.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace giga_edge::vits {

TextModel OpenTextModel(
  const std::string& path,
  const std::string& environment,
  const IDataReaderPtr& data_reader,
  const std::map<std::string, EnvironmentPtr>* environments,
  bool in_place
) {
  const auto name = path.substr(path.rfind('/') + 1); // the whole path when it has no '/'
  const auto models = std::make_shared<DirectoryReader>(data_reader, path.substr(0, path.size() - name.size()));
  InterpreterParams params;
  params.type = InterpreterParams::InterpreterType::ONNX;
  params.onnx_params.use_environment = environment;
  if (in_place) {
    // The ORT format is a flatbuffer with the identifier "ORTM" at byte 4.
    const auto file = EnsureNotNull(models->ReadPersistentData(name), path);
    const auto bytes = file->View();
    if (bytes.substr(0, 8).substr(std::min<size_t>(bytes.size(), 4)) != "ORTM") {
      throw std::invalid_argument("vits: 'mmap_models' takes models in the ORT format, '" + path + "' is not");
    }
    params.onnx_params.use_ort_format = true;
    params.onnx_params.disable_copy_to_ram = true;
  }
  try {
    // The session first: GCC before 13 does not destroy a built member of an aggregate when the next one throws.
    auto session = MakeInterpreter(params, name, models, environments);
    return {path, std::move(session)};
  } catch (const std::exception& error) {
    throw std::runtime_error("vits: model '" + path + "': " + error.what());
  }
}

void RunTextModel(const TextModel& model) {
  try {
    model.session->Invoke();
  } catch (const std::exception& error) {
    ThrowTextModelError(model, error.what());
  }
}

void ThrowTextModelError(const TextModel& model, const std::string& what) {
  throw std::runtime_error("vits: model '" + model.path + "': " + what);
}

void CheckTextModel(
  const TextModel& model, const std::vector<TensorSpec>& inputs, const std::vector<TensorSpec>& outputs
) {
  const auto& path = model.path;
  const auto& interpreter = model.session;
  const auto check = [&](const std::vector<TensorSpec>& specs, const std::vector<std::string>& names, bool input) {
    const std::string kind = input ? "input" : "output";
    for (const auto& spec : specs) {
      if (std::find(names.begin(), names.end(), spec.name) == names.end()) {
        throw std::runtime_error("vits: model '" + path + "' has no " + kind + " '" + spec.name + "'");
      }
      const auto tensor = input ? interpreter->Input(spec.name) : interpreter->Output(spec.name);
      if (tensor->GetDataType() != spec.type || tensor->DimsNum() != spec.rank) {
        throw std::runtime_error(
          "vits: model '" + path + "': the " + kind + " '" + spec.name + "' has another type or rank than expected"
        );
      }
    }
  };
  check(inputs, interpreter->InputNames(), true);
  check(outputs, interpreter->OutputNames(), false);
  if (interpreter->InputNames().size() != inputs.size()) {
    throw std::runtime_error("vits: model '" + path + "' has inputs the stage does not fill");
  }
}

size_t TakePositions(std::string& vocabulary, const std::string& name, size_t least) {
  constexpr std::string_view key = "[positions]\t";
  for (size_t begin = 0; begin < vocabulary.size();) {
    auto end = vocabulary.find('\n', begin);
    end = end == std::string::npos ? vocabulary.size() : end;
    if (vocabulary.compare(begin, key.size(), key) == 0) {
      auto number = std::string_view(vocabulary).substr(begin + key.size(), end - begin - key.size());
      if (!number.empty() && number.back() == '\r') {
        number.remove_suffix(1);
      }
      size_t positions = 0;
      const auto parsed = std::from_chars(number.data(), number.data() + number.size(), positions);
      if (
        parsed.ec != std::errc() || parsed.ptr != number.data() + number.size() || positions < least
        || positions > kMaxPositions
      ) {
        const auto line = std::count(vocabulary.begin(), vocabulary.begin() + static_cast<std::ptrdiff_t>(begin), '\n');
        ThrowTableError(name, static_cast<size_t>(line) + 1, "bad [positions] line");
      }
      vocabulary.erase(begin, std::min(end + 1, vocabulary.size()) - begin);
      return positions;
    }
    begin = end + 1;
  }
  throw std::runtime_error("vits: model vocabulary '" + name + "' has no [positions] line");
}

std::vector<int> TensorShape(std::initializer_list<size_t> sizes) {
  constexpr size_t limit = std::numeric_limits<int>::max();
  std::vector<int> shape;
  size_t elements = 1;
  for (const auto size : sizes) {
    if (size > limit || (size != 0 && elements > limit / size)) {
      throw std::length_error("vits: text model: the input is too large for a tensor");
    }
    elements *= size;
    shape.push_back(static_cast<int>(size));
  }
  return shape;
}

} // namespace giga_edge::vits
