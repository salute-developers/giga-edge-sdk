#!/usr/bin/env bash
set -ex

grammar="string_grammar_all"
export_name="STRING_GRAMMAR_ALL"

thraxcompiler --input_grammar="${grammar}.grm" --output_far="${grammar}.far"
farextract --filename_suffix=.fst "${grammar}.far"
mv "${export_name}.fst" "${grammar}.fst"
rm -f "${grammar}.far"
