#!/usr/bin/env python3
"""Writes the tiny stand-ins for a homograph model that reads a marked word: homographs_marked.onnx with its
vocabulary homographs_marked_vocab.txt, homographs_marked_int32.onnx, homographs_marked_shape.onnx and
homographs_marked_ort.onnx.

Inputs, int64: input_ids [rows, tokens], homo_start_ids [rows], homo_end_ids [rows]. Output logits, float [rows, 1].
The vocabulary: [PAD] [UNK] [CLS] [SEP], the marks ".,!?-", the Russian letters in lower and in upper case, the
lower-case letters as "##" continuations, [HOMO], [/HOMO], and the line "[positions]<TAB>512". A word of other
symbols is one [UNK].
A row is well-formed when it starts with [CLS] and has [HOMO] and [/HOMO] at the given places. Its logit is then
    start + end - tokens + 0.5 + 10 * (the number of "!", "ё" and "##ё" in the row)
    + 1000 * (the number of [CLS] and [SEP] in the row - 2),
where `tokens` is the width of the batch: the padding counts, so the answer depends on the longest row. Without the
last two terms the second reading is chosen when more tokens stand before the word than after it; the last term is
zero unless the row is padded with a service token. For a row that is not well-formed the logit is zero: the first
reading with the probability 0.5. A token "ъ" or "##ъ" makes the logit NaN. An id outside the vocabulary or a
place outside the row fails the run.
homographs_marked_int32.onnx is the same graph with int32 inputs: the stage must refuse it.
homographs_marked_shape.onnx gives two logits for a row: it loads and fails at its first run.
homographs_marked_ort.onnx is the model in the ORT format, for the models used in place from their files.

  python3 make_marked_homograph_model.py     (needs onnx, and onnxruntime not newer than that of the SDK)
"""
import os
import shutil
import subprocess
import sys
import tempfile

import onnx
from onnx import TensorProto, helper

LETTERS = 'абвгдежзийклмнопрстуфхцчшщъыьэюяё'
VOCABULARY = ['[PAD]', '[UNK]', '[CLS]', '[SEP]'] + list('.,!?-') + list(LETTERS) + list(LETTERS.upper()) \
    + ['##' + letter for letter in LETTERS] + ['[HOMO]', '[/HOMO]']
ID = {token: number for number, token in enumerate(VOCABULARY)}
POSITIONS = 512
BONUS = ('!', 'ё', '##ё')


def model(int_type, logits=1):
    inputs = [helper.make_tensor_value_info('input_ids', int_type, ['rows', 'tokens']),
              helper.make_tensor_value_info('homo_start_ids', int_type, ['rows']),
              helper.make_tensor_value_info('homo_end_ids', int_type, ['rows'])]
    output = helper.make_tensor_value_info('logits', TensorProto.FLOAT, ['rows', logits])
    nodes, initializers = [], []

    def const(name, value, kind=TensorProto.INT64):
        initializers.append(helper.make_tensor(name, kind, [] if not isinstance(value, list) else [len(value)],
                                               value if isinstance(value, list) else [value]))
        return name

    def node(kind, inputs, name, **attributes):
        nodes.append(helper.make_node(kind, inputs, [name], **attributes))
        return name

    axis = const('axis_1', [1])
    ids = node('Cast', ['input_ids'], 'ids', to=TensorProto.INT64)
    start = node('Unsqueeze', [node('Cast', ['homo_start_ids'], 'start_flat', to=TensorProto.INT64), axis], 'start')
    end = node('Unsqueeze', [node('Cast', ['homo_end_ids'], 'end_flat', to=TensorProto.INT64), axis], 'end')
    tokens = node('Gather', [node('Shape', [ids], 'shape'), const('one_index', 1)], 'tokens')

    def at(place, token, name):
        return node('Equal', [node('GatherElements', [ids, place], name + '_id', axis=1), const('id_' + name, ID[token])],
                    name + '_ok')

    first = node('Equal', [node('Gather', [ids, const('zero_index', [0])], 'first', axis=1), const('id_cls', ID['[CLS]'])],
                 'cls_ok')
    ok = node('And', [node('And', [first, at(start, '[HOMO]', 'open')], 'ok_open'), at(end, '[/HOMO]', 'close')], 'ok')
    place = node('Sub', [node('Add', [start, end], 'places'), tokens], 'place')
    table = [10.0 if token in BONUS else 1000.0 if token in ('[CLS]', '[SEP]') else
             float('nan') if token in ('ъ', '##ъ') else 0.0 for token in VOCABULARY]
    bonus = node('ReduceSum', [node('Gather', [const('table', table, TensorProto.FLOAT), ids], 'weights'), axis], 'bonus',
                 keepdims=1)
    score = node('Add', [node('Add', [node('Cast', [place], 'place_float', to=TensorProto.FLOAT),
                                      const('half', 0.5 - 2000.0, TensorProto.FLOAT)], 'based'), bonus], 'score')
    one = node('Mul', [score, node('Cast', [ok], 'gate', to=TensorProto.FLOAT)], 'one' if logits > 1 else 'logits')
    if logits > 1:
        nodes.append(helper.make_node('Concat', [one] * logits, ['logits'], axis=1))
    graph = helper.make_graph(nodes, 'marked_homograph_stub', inputs, [output], initializers)
    result = helper.make_model(graph, opset_imports=[helper.make_opsetid('', 13)])
    result.ir_version = 8
    onnx.checker.check_model(result)
    return result


here = os.path.dirname(os.path.abspath(__file__))
onnx.save(model(TensorProto.INT64), os.path.join(here, 'homographs_marked.onnx'))
onnx.save(model(TensorProto.INT32), os.path.join(here, 'homographs_marked_int32.onnx'))
onnx.save(model(TensorProto.INT64, logits=2), os.path.join(here, 'homographs_marked_shape.onnx'))
with open(os.path.join(here, 'homographs_marked_vocab.txt'), 'w', encoding='utf-8') as vocabulary:
    vocabulary.write('\n'.join(VOCABULARY) + f'\n[positions]\t{POSITIONS}\n')
with tempfile.TemporaryDirectory() as work:
    shutil.copy(os.path.join(here, 'homographs_marked.onnx'), work)
    subprocess.run([sys.executable, '-m', 'onnxruntime.tools.convert_onnx_models_to_ort', work, '--output_dir',
                    os.path.join(work, 'ort'), '--optimization_style', 'Fixed'], check=True, capture_output=True)
    shutil.copy(os.path.join(work, 'ort', 'homographs_marked.ort'), os.path.join(here, 'homographs_marked_ort.onnx'))
