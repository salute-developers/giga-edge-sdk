#!/usr/bin/env bash
set -ex

thraxcompiler --input_grammar=float_grammar_first.grm --output_far=float_grammar_first.far
farextract --filename_suffix=.fst float_grammar_first.far
rm float_grammar_first.far

thraxcompiler --input_grammar=float_grammar_second.grm --output_far=float_grammar_second.far
farextract --filename_suffix=.fst float_grammar_second.far
rm float_grammar_second.far
