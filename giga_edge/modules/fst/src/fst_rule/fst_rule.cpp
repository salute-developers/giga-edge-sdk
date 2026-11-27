#include "giga_edge/modules/fst/fst_rule.h"

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <fst/compose.h>
#include <fst/const-fst.h>
#include <fst/log.h>
#include <fst/project.h>
#include <fst/shortest-path.h>
#include <fst/vector-fst.h>
#include <thrax/paths.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace giga_edge {

namespace {

using Fst = fst::Fst<fst::StdArc>;
using FstPtr = std::unique_ptr<Fst>;

fst::TokenType DetectInputTokenType(const Fst& rule) {
  for (fst::StateIterator<Fst> state_iter(rule); !state_iter.Done(); state_iter.Next()) {
    for (fst::ArcIterator<Fst> arc_iter(rule, state_iter.Value()); !arc_iter.Done(); arc_iter.Next()) {
      const auto& arc = arc_iter.Value();
      if (arc.ilabel > 255) {
        return fst::TokenType::UTF8;
      }
    }
  }
  return fst::TokenType::BYTE;
}

fst::TokenType DetectOutputTokenType(const Fst& rule) {
  for (fst::StateIterator<Fst> state_iter(rule); !state_iter.Done(); state_iter.Next()) {
    for (fst::ArcIterator<Fst> arc_iter(rule, state_iter.Value()); !arc_iter.Done(); arc_iter.Next()) {
      const auto& arc = arc_iter.Value();
      if (arc.olabel > 255) {
        return fst::TokenType::UTF8;
      }
    }
  }
  return fst::TokenType::BYTE;
}

class FstRuleImpl : public IFstRule {
public:
  explicit FstRuleImpl(FstPtr rule)
    : rule_(std::move(rule)) {
    if (rule_ == nullptr) {
      throw std::runtime_error("Failed to parse rule");
    }
    input_token_type_ = DetectInputTokenType(*rule_);
    output_token_type_ = DetectOutputTokenType(*rule_);
  }

  std::string Apply(const std::string& input) const override {
    if (input.empty()) {
      return "";
    }
    fst::StdVectorFst input_fst;
    const fst::StringCompiler<fst::StdArc> compiler(input_token_type_);
    if (!compiler(input, &input_fst)) {
      std::cerr << "FST: Failed to compile fst from input sequence" << std::endl;
      return "";
    }

    fst::StdVectorFst output_fst;
    fst::ComposeOptions compose_options(true, fst::ALT_SEQUENCE_FILTER);
    fst::Compose(input_fst, *rule_, &output_fst, compose_options);

    fst::StdVectorFst output_fst_shortest;
    fst::ShortestPath(output_fst, &output_fst_shortest, 1);

    if (output_fst_shortest.Start() == fst::kNoStateId) {
      return "";
    }

    rule_->Start();

    std::vector<fst::StdArc::Label> output_labels;
    for (fst::PathIterator<fst::StdArc> iter(output_fst_shortest, false); !iter.Done(); iter.Next()) {
      for (const auto label : iter.OLabels()) {
        if (label != 0) {
          output_labels.push_back(label);
        }
      }
    }

    std::string output;
    if (!fst::LabelsToString(output_labels, &output, output_token_type_)) {
      std::cerr << "FST: Failed to convert output labels to string" << std::endl;
      return "";
    }
    return output;
  }

  std::vector<std::string> ApplyAll(const std::string& input, const std::size_t max_outputs) const override {
    if (input.empty() || max_outputs == 0) {
      return {};
    }

    fst::StdVectorFst input_fst;
    const fst::StringCompiler<fst::StdArc> compiler(input_token_type_);
    if (!compiler(input, &input_fst)) {
      std::cerr << "FST: Failed to compile fst from input sequence" << std::endl;
      return {};
    }

    fst::StdVectorFst output_fst;
    fst::ComposeOptions compose_options(true, fst::ALT_SEQUENCE_FILTER);
    fst::Compose(input_fst, *rule_, &output_fst, compose_options);
    fst::Project(&output_fst, fst::ProjectType::OUTPUT);

    const auto bounded_max_outputs = static_cast<int32_t>(
      std::min(max_outputs, static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
    );
    fst::StdVectorFst output_fst_shortest;
    fst::ShortestPath(output_fst, &output_fst_shortest, bounded_max_outputs, true);
    if (output_fst_shortest.Start() == fst::kNoStateId) {
      return {};
    }

    std::vector<std::string> outputs;
    for (fst::PathIterator<fst::StdArc> iter(output_fst_shortest, false); !iter.Done(); iter.Next()) {
      std::vector<fst::StdArc::Label> output_labels;
      for (const auto label : iter.OLabels()) {
        if (label != 0) {
          output_labels.push_back(label);
        }
      }

      std::string output;
      if (!fst::LabelsToString(output_labels, &output, output_token_type_)) {
        std::cerr << "FST: Failed to convert output labels to string" << std::endl;
        continue;
      }
      outputs.push_back(std::move(output));
    }
    return outputs;
  }

private:
  FstPtr rule_;
  fst::TokenType input_token_type_;
  fst::TokenType output_token_type_;
};

// The type comes from the header, not from the OpenFst type registry: in a static build the linker drops the type
// registrations.
FstPtr ReadFst(std::istream& in, const fst::FstReadOptions& options) {
  const auto start = in.tellg();
  fst::FstHeader header;
  if (!header.Read(in, options.source)) {
    return nullptr;
  }
  in.seekg(start);
  if (header.FstType() == "const") {
    return FstPtr(fst::ConstFst<fst::StdArc>::Read(in, options));
  }
  return FstPtr(fst::VectorFst<fst::StdArc>::Read(in, options));
}

// A const FST stored as a file (a member of an uncompressed archive or a file of a directory) is mapped straight from
// it, without parsing and without a heap copy (fstconvert --fst_type=const --fst_align); everything else goes through
// the reader.
FstPtr ReadFstFromFile(IDataReader& data_reader, const std::string& path) {
  std::string file;
  std::size_t begin = 0;
  std::size_t size = 0;
  if (const auto member = data_reader.GetArchiveMemberLocation(path)) {
    file = member->archive_path;
    begin = member->offset;
    size = member->size;
  } else if (const auto prefix = data_reader.GetPathPrefix()) {
    file = *prefix + path; // same as the directory reader
    std::error_code error;
    size = std::filesystem::file_size(file, error);
    if (error) {
      return nullptr;
    }
  } else {
    return nullptr;
  }
  std::ifstream in(file, std::ios::binary);
  in.seekg(static_cast<std::streamoff>(begin));
  // Not an FST (for example, a file in the reader's format): silently fall back to the reader.
  std::int32_t magic = 0;
  if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic)) || magic != fst::kFstMagicNumber) {
    return nullptr;
  }
  in.seekg(static_cast<std::streamoff>(begin));
  fst::FstHeader header;
  if (!header.Read(in, file) || header.FstType() != "const") {
    return nullptr;
  }
  in.seekg(static_cast<std::streamoff>(begin));
  fst::FstReadOptions options(file);
  options.mode = fst::FstReadOptions::MAP;
  auto rule = FstPtr(fst::ConstFst<fst::StdArc>::Read(in, options));
  // The mapping takes as much as the header promises: a truncated file read through the reader gives a proper error.
  const auto end = in.tellg();
  if (rule == nullptr || end < 0 || static_cast<std::size_t>(end) > begin + size) {
    return nullptr;
  }
  return rule;
}

} // namespace

FstRulePtr ReadFstRule(IDataReader& data_reader, const std::string& path) {
  if (auto rule = ReadFstFromFile(data_reader, path)) {
    return std::make_shared<FstRuleImpl>(std::move(rule));
  }
  // Not a file on disk or it could not be read (for example, a file in the reader's format): go through the reader.
  auto data = EnsureNotNull(data_reader.ReadTransientData(path), path)->Extract();
  std::string data_str(data.data(), data.size());
  std::istringstream data_stream(data_str);
  return std::make_shared<FstRuleImpl>(ReadFst(data_stream, fst::FstReadOptions(path)));
}

FstRulePtr ReadFstRule(const DataReaderPtr& data_reader, const std::string& path) {
  if (!data_reader) {
    throw std::runtime_error("Data reader is null");
  }
  return ReadFstRule(*data_reader, path);
}

} // namespace giga_edge