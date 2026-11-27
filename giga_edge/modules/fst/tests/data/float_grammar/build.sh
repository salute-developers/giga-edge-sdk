#!/usr/bin/env bash
set -ex

thraxcompiler --input_grammar=float_grammar.grm --output_far=float_grammar.far
farextract --filename_suffix=.fst float_grammar.far
rm float_grammar.far

# The same rule as an aligned const FST, a copy cut in half, and each FST alone in an uncompressed tar.
fstconvert --fst_type=const --fst_align float_grammar.fst float_grammar_const.fst
head -c $(($(wc -c < float_grammar_const.fst) / 2)) float_grammar_const.fst > float_grammar_const_truncated.fst
python3 - <<'PY'
import tarfile
for name in ("float_grammar", "float_grammar_const", "float_grammar_const_truncated"):
    with tarfile.open(f"{name}.tar", "w", format=tarfile.USTAR_FORMAT) as tar:
        info = tar.gettarinfo(f"{name}.fst")
        info.mtime, info.uid, info.gid, info.uname, info.gname, info.mode = 0, 0, 0, "", "", 0o644
        with open(f"{name}.fst", "rb") as f:
            tar.addfile(info, f)
PY
