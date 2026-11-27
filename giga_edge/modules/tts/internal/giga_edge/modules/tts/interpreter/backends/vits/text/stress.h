#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/prepared_sentence.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/homograph_classifier.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/homograph_model.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/homographs.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/stress_dict.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/stress_model.h"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge::vits {

// The stage reads its files from the text directory of the model package by fixed names:
//   stress.dict           the stresses of known words, a word dictionary (stress_dict.h); required
//   yo.dict               words spelled with 'е' that read with 'ё', a word dictionary
//   yo_extra.tsv          more of them: word<TAB>word with 'ё'; a word equal to itself is kept from yo.dict
//   homographs.tsv        the reading of a homograph by its neighbours (homographs.h)
//   stress_overrides.tsv  word<TAB>word with '+' before the stressed vowels: stronger than the dictionary
//   hyphen.tsv            parts of hyphenated words: prefix|suffix<TAB>part without a stress,
//                         letters<TAB>names of letters by commas, yo<TAB>part before a hyphen that reads with 'ё'
//   no_pause_words.txt    a phrase per line: a comma right before it or right after it takes no pause
//   e_to_eh.tsv           words where 'е' reads as 'э' (the format is at its reader in stress.cpp)
//   stress.onnx           the model of unknown words (stress_model.h); with it stress_vocab.txt is required
//   homographs.onnx       the model of homographs; with it homographs_vocab.txt is required. Its kind is told by
//                         its inputs: a model of pairs "sentence, reading" (homograph_model.h) or a model of a
//                         marked word (homograph_classifier.h)
// With a model of pairs:
//   homographs2.onnx      a second model of the same vocabulary: the probabilities of the two are averaged
//   homographs.dict       the words the model reads besides those of homographs.tsv: a word dictionary of the
//                         stressed vowels of all the readings of a word
// With a model of a marked word:
//   homographs_variants.tsv  the words the model reads and their two readings; required
//   homographs2.onnx         a model of pairs next to it, the small one, with its vocabulary homographs2_vocab.txt
//                            and homographs.dict: it reads the words the first one does not know and may agree
//                            with a reading of the first one that does not prevail (homographs_priors.tsv)
// With or without models:
//   homographs_priors.tsv    word<TAB>its prevailing reading<TAB>strong, mild or none: how much the reading
//                            prevails in common text. The reading is the default of a word outside homographs.tsv
//                            instead of the stress dictionary; the other reading of a strong or a mild word is
//                            taken from the model of a marked word only when the model is very sure
//                            (StressParams::homograph_strong_confidence, homograph_mild_confidence)
// A file that is not there turns its part off; only stress.dict is required.

// Who reads a homograph when a model of homographs is in the package; without a model it is always the rules.
enum class HomographMode {
  RULES,           // the rules and defaults of homographs.tsv; a word outside it takes its prevailing reading
                   // (homographs_priors.tsv) or goes on to the stress dictionary
  RULES_THEN_MODEL // a rule of the word that holds decides; otherwise the models when they are sure: the model of
                   // a marked word, kept from a reading that does not prevail (homographs_priors.tsv) unless it
                   // is very sure or the model of pairs gives the same one, then the model of pairs; otherwise
                   // the default of homographs.tsv or, for a word outside it, the prevailing reading or the
                   // stress dictionary. No model is asked of a word of the table with readings in 'ё'; a model
                   // of pairs is not asked of a word with one reading in the table either
};

struct StressParams {
  HomographMode homographs = HomographMode::RULES_THEN_MODEL;
  double homograph_confidence = 0.5; // the model decides with this probability of its reading or a higher one; 0..1
  // The model of a marked word gives a reading that does not prevail for a word of homographs_priors.tsv: it is
  // taken with this probability of the reading, or when the model of pairs gives the same one; otherwise the default
  // stays. For a word whose reading prevails strongly and for one whose reading prevails mildly; a value that is not
  // above homograph_confidence leaves such words to the model. 0..1
  double homograph_strong_confidence = 0.9;
  double homograph_mild_confidence = 0.9;
  std::string environment;          // a shared ONNX environment for the models; empty: own sessions of one thread
  bool verify_dictionaries = false; // check the checksums of the dictionaries at load: reads the files whole
  bool mmap_models = false;         // the models are in the ORT format and work from the mapped files (text_model.h)
};

enum class StressSource {
  NONE,
  USER,
  CLITIC,
  OVERRIDE,
  HOMOGRAPH_RULE,
  HOMOGRAPH_DEFAULT,
  HOMOGRAPH_MODEL,
  NO_VOWELS,
  ONE_VOWEL,
  DICTIONARY,
  YO,
  MODEL,
  FALLBACK
};

// A word, a part of a hyphenated word or a separator of one symbol, in the order of the text.
struct StressItem {
  enum class Join { NONE, NEXT, PREVIOUS };

  bool is_word = false;
  std::string written;                      // as it stands in the text, without stress marks
  std::string text;                         // with 'ё' restored
  std::string stressed;                     // a '+' before every stressed vowel
  StressSource source = StressSource::NONE; // what decided the stress of the word
  Join join = Join::NONE;                   // the part reads as one word with this neighbour part
  bool compound = false;                    // the first part of a compound word in -о/-е: stressed as its stem
};

// The text with a '+' before every stressed letter.
std::string StressedText(const std::vector<StressItem>& items);

// Stress, 'ё' and pauses of a normalized sentence: lower-case Russian words and marks, next to the words or after a
// space. A '+' before a vowel is the stress set by the user; any other '+' is dropped. The order: user marks,
// hyphenated words, 'ё', homographs, overrides, one vowel, the dictionary, the model. Every word of the result that
// has a vowel has a stress. An object keeps no global state and prints nothing; calls to one object must not overlap.
class Stresser {
public:
  // Reads the files of `params` from `directory` of the reader; throws std::runtime_error naming the bad file.
  Stresser(
    const StressParams& params,
    const std::string& directory,
    const IDataReaderPtr& data_reader,
    const std::map<std::string, EnvironmentPtr>* environments = nullptr
  );

  // Throws std::invalid_argument on text that is not UTF-8.
  std::vector<StressItem> Stress(std::string_view text);

  // The sentence in both spellings. marked: an upper-case vowel for a stressed one and for every 'ё', '^' pauses
  // at the marks (none at a comma next to a no-pause phrase). plain: the words as they are written, without the
  // stress marks ('ё' is not restored there). In both the marks stand next to the words: no space before
  // ". , : ; ? ! )" and a closing quote, none after "(" and an opening quote. A word is a run of the parts joined
  // by StressItem::Join.
  // marked holds only the symbols of the voice `alphabet` that the stage may write at all: the Russian lower-case
  // letters, the upper-case vowels, the space, the marks . , ; : ? ! - ( ) " and its own '^'. Anything else, a '^' of
  // the text too, is dropped and appended to `dropped`: a separator reads as a space in both spellings, an upper-case
  // vowel falls back to the lower-case one, any other letter leaves marked only.
  PreparedSentence Prepare(
    const std::vector<StressItem>& items, std::u32string_view alphabet, std::string* dropped = nullptr
  ) const;

  PreparedSentence Prepare(std::string_view text, std::u32string_view alphabet, std::string* dropped = nullptr) {
    return Prepare(Stress(text), alphabet, dropped);
  }

  // The word is in the stress dictionary, or the dictionary of 'ё' has it: written with 'е', or with 'ё' where the
  // dictionary has 'ё'. The word is taken as the stage takes its words, in lower case; no table and no model is
  // asked. Reads the dictionaries only: may be called from any thread while the object lives, during Stress too.
  bool KnowsWord(std::string_view word) const;

private:
  using Words = std::set<std::string, std::less<>>;
  using WordMap = std::map<std::string, std::string, std::less<>>;

  std::vector<StressItem> Parse(std::string_view text) const;
  void MarkHyphenated(StressItem** parts, size_t count) const;
  void RestoreYo(StressItem& part) const;
  void ReadHomographs(std::vector<StressItem>& items, const std::vector<StressItem*>& parts);
  void StressWord(StressItem& part);
  bool StressCompound(StressItem& part) const;

  std::optional<StressDict> dictionary_;
  std::optional<StressDict> yo_;
  WordMap yo_extra_;
  WordMap overrides_;
  WordMap e_to_eh_;  // a word -> the word with 'э'
  WordMap yo_first_; // a part before a hyphen, with 'е' -> with 'ё'
  std::optional<HomographTable> homographs_;
  Words prefixes_;
  Words suffixes_;
  Words letter_names_;
  std::vector<std::vector<std::string>> no_pause_;
  std::unique_ptr<StressModel> model_;
  std::optional<StressDict> homograph_readings_;
  std::unique_ptr<HomographModel> homograph_model_;
  std::unique_ptr<HomographClassifier> homograph_classifier_;
  HomographMode homograph_mode_ = HomographMode::RULES;
  double homograph_confidence_ = 0.0;
  struct Prior {
    std::string reading; // the prevailing one
    double confidence;   // of the model for the other reading; 0: any
  };
  std::map<std::string, Prior, std::less<>> priors_;
};

} // namespace giga_edge::vits
