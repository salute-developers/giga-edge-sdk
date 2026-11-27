#!/usr/bin/env python3
"""Writes the tiny stand-ins for the homograph model: text/ru/homographs.onnx with its vocabulary
text/ru/homographs_vocab.txt, homographs_int32.onnx, homographs_second.onnx and homographs_shape.onnx.

Inputs, int64 [readings, tokens]: input_ids, attention_mask. Output logits, float [readings, 2].
The vocabulary: [PAD] [UNK] [CLS] [SEP], marks, the letters, the letters as "##" continuations, the piece "за",
<w>, </w>, and the line "[positions]<TAB>64".
A row is well-formed when it starts with [CLS], has two [SEP], one <w> before one </w>, and its mask is ones on the
tokens and zeros on the padding. Let d be the place of the token "+" in the reading, counted from the first [SEP]
(2 for "з+амок", 3 for "за ##м + о ##к"). For a well-formed row the logit of class 1 is d when <w> stands at an even
position of the row and 6 - d otherwise; the logit of class 0 is zero. So the chosen reading is the one stressed
later when <w> is at an even position and the one stressed earlier otherwise, with the probability
1 / (1 + exp(-logit)): 0.73, 0.88, 0.953, 0.982, 0.993 for 1..5. The answer depends on where the homograph is
marked, on the tokens of the reading and on the padding. For a row that is not well-formed both logits are zero:
the first reading with the probability 0.5. Every token "э" or "##э" of a well-formed row adds 3 to its logit of
class 0. A token "ъ" or "##ъ" makes the logits NaN. An id outside the vocabulary fails the run.
homographs_int32.onnx is the same graph with int32 inputs: the stage must refuse it. homographs_second.onnx swaps the
even and the odd positions of <w>: a stand-in for the second model of a package. homographs_shape.onnx gives
logits of three classes: it loads and fails at its first run.

  python3 make_homograph_model.py     (needs the onnx package)
"""
import os

import onnx
from onnx import TensorProto, helper

LETTERS = 'абвгдежзийклмнопрстуфхцчшщъыьэюяё'
VOCABULARY = ['[PAD]', '[UNK]', '[CLS]', '[SEP]'] + list('.,!?-+:;()"') + list(LETTERS) \
    + ['##' + letter for letter in LETTERS] + ['за', '<w>', '</w>']
ID = {token: number for number, token in enumerate(VOCABULARY)}
POSITIONS = 64


def model(int_type, second=False, classes=2):
    inputs = [helper.make_tensor_value_info(name, int_type, ['readings', 'tokens'])
              for name in ('input_ids', 'attention_mask')]
    output = helper.make_tensor_value_info('logits', TensorProto.FLOAT, ['readings', classes])
    nodes, initializers = [], []

    def const(name, value, kind=TensorProto.INT64):
        initializers.append(helper.make_tensor(name, kind, [] if not isinstance(value, list) else [len(value)],
                                               value if isinstance(value, list) else [value]))
        return name

    def node(kind, inputs, name, **attributes):
        nodes.append(helper.make_node(kind, inputs, [name], **attributes))
        return name

    ids = node('Cast', ['input_ids'], 'ids', to=TensorProto.INT64)
    mask = node('Cast', ['attention_mask'], 'mask', to=TensorProto.INT64)
    count = node('Gather', [node('Shape', [ids], 'shape'), const('one_index', 1)], 'count')
    positions = node('Range', [const('zero', 0), count, const('one', 1)], 'positions')
    axis = const('axis_1', [1])

    def is_token(token):
        name = token.strip('[]<>/+') or 'plus'
        name = {'w': 'open' if token == '<w>' else 'close'}.get(name, name)
        return node('Cast', [node('Equal', [ids, const('id_' + name, ID[token])], 'is_bool_' + name)], 'is_' + name,
                    to=TensorProto.INT64)

    def total(what, name):
        return node('ReduceSum', [what, axis], name, keepdims=0)

    def index(what, name):
        return total(node('Mul', [what, positions], name + '_at'), name)

    plus_index = index(is_token('+'), 'plus_index')
    is_open, is_close, is_sep, is_pad = is_token('<w>'), is_token('</w>'), is_token('[SEP]'), is_token('[PAD]')
    open_index, close_index = index(is_open, 'open_index'), index(is_close, 'close_index')
    first = node('Gather', [ids, const('zero_index', 0)], 'first', axis=1)
    tokens = node('Sub', [count, total(is_pad, 'pads')], 'row_tokens')
    checks = [
        node('Equal', [first, const('id_cls', ID['[CLS]'])], 'cls_ok'),
        node('Equal', [total(is_sep, 'seps'), const('two', 2)], 'seps_ok'),
        node('Equal', [total(is_open, 'opens'), 'one'], 'open_ok'),
        node('Equal', [total(is_close, 'closes'), 'one'], 'close_ok'),
        node('Less', [open_index, close_index], 'order_ok'),
        node('Equal', [total(mask, 'mask_sum'), tokens], 'mask_ok'),
        node('Equal', [total(node('Mul', [mask, is_pad], 'mask_on_pad'), 'mask_pads'), 'zero'], 'pad_ok'),
    ]
    ok = checks[0]
    for number, check in enumerate(checks[1:]):
        ok = node('And', [ok, check], f'ok_{number}')
    # The first [SEP]: the least position among those of [SEP].
    not_sep = node('Sub', ['one', is_sep], 'not_sep')
    first_sep = node('ReduceMin', [node('Add', [positions, node('Mul', [not_sep, const('far', 10000)], 'sep_far')],
                                        'sep_positions')], 'first_sep', axes=[1], keepdims=0)
    place = node('Sub', [plus_index, first_sep], 'place')
    even = node('Equal', [node('Mod', [open_index, 'two'], 'parity'), 'one' if second else 'zero'], 'even')
    signed = node('Where', [even, place, node('Sub', [const('six', 6), place], 'six_minus_place')], 'signed')
    embedded = total(node('Gather', [const('table', [0] * len(VOCABULARY)), ids], 'embedded_ids'), 'embedded')
    score = node('Mul', [node('Add', [signed, embedded], 'score'), node('Cast', [ok], 'ok_int', to=TensorProto.INT64)],
                 'gated')
    def weights(tokens, value, name):
        table = [value if token in tokens else 0.0 for token in VOCABULARY]
        return node('ReduceSum', [node('Gather', [const(name + '_table', table, TensorProto.FLOAT), ids], name + '_rows'),
                                  axis], name, keepdims=1)

    gate = node('Unsqueeze', [node('Cast', [ok], 'ok_float', to=TensorProto.FLOAT), axis], 'gate')
    not_a_number = weights(('ъ', '##ъ'), float('nan'), 'not_a_number')
    one_logit = node('Add', [node('Unsqueeze', [node('Cast', [score], 'score_float', to=TensorProto.FLOAT), axis],
                                  'one_score'), not_a_number], 'one_logit')
    zero_logit = node('Mul', [weights(('э', '##э'), 3.0, 'zero_score'), gate], 'zero_logit')
    nodes.append(helper.make_node('Concat', [zero_logit, one_logit] + [zero_logit] * (classes - 2), ['logits'], axis=1))
    graph = helper.make_graph(nodes, 'homograph_stub', inputs, [output], initializers)
    result = helper.make_model(graph, opset_imports=[helper.make_opsetid('', 13)])
    result.ir_version = 8
    onnx.checker.check_model(result)
    return result


here = os.path.dirname(os.path.abspath(__file__))
onnx.save(model(TensorProto.INT64), os.path.join(here, 'text', 'ru', 'homographs.onnx'))
onnx.save(model(TensorProto.INT32), os.path.join(here, 'homographs_int32.onnx'))
onnx.save(model(TensorProto.INT64, second=True), os.path.join(here, 'homographs_second.onnx'))
onnx.save(model(TensorProto.INT64, classes=3), os.path.join(here, 'homographs_shape.onnx'))
with open(os.path.join(here, 'text', 'ru', 'homographs_vocab.txt'), 'w', encoding='utf-8') as vocabulary:
    vocabulary.write('\n'.join(VOCABULARY) + f'\n[positions]\t{POSITIONS}\n')
