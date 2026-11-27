#!/usr/bin/env python3
"""Writes the tiny stand-ins for the stress model of unknown words: text/ru/stress.onnx and the models the stage
must refuse or fail on: stress_int32.onnx, stress_extra_input.onnx, stress_rank.onnx, stress_integer.onnx,
stress_shape.onnx.

Inputs, int64 [batch, length]: input_ids, attention_mask, token_type_ids. Output logits, float [batch, length, 3]:
no stress, stress, secondary stress. With s = (input_ids * attention_mask + token_type_ids) / 10 the logits of a
position are (2s, s, 0) when the input is well-formed: the first id is [bos] (2), the last one is [eos] (3), no id
is [pad] (0), the mask is all ones and the types are all zeros. Then the logit of the stress grows with the id of
the letter and its probability falls: the most probable stressed vowel is the first one of the alphabet. At the
letter 'э' the logit of the secondary stress is 2s as well: the stress there is less probable than at the next
letter 'ю', though its logit over the larger of the other two is higher. For any other input the logits are
(0, s, 0) and the answer turns to the last vowel of the alphabet. The letter 'ъ' makes every logit of the word
NaN. An id of 64 or more fails the run.
The other models: int32 inputs; one more input, position_ids, added to the types; logits of rank 2; logits of
64-bit integers; logits of two classes (this one loads and fails at its first run).
text_external/stress.onnx is the first model with its table in the file stress.onnx.data next to it.

  python3 make_stress_model.py     (needs the onnx package)
"""
import os
import struct

import onnx
from onnx import TensorProto, helper

BOS, EOS, IDS = 2, 3, 64
here = os.path.dirname(os.path.abspath(__file__))
LETTERS = [line.rstrip('\n') for line in open(os.path.join(here, 'text', 'ru', 'stress_vocab.txt'), encoding='utf-8')]
SECONDARY, NOT_A_NUMBER = LETTERS.index('э'), LETTERS.index('ъ')


def model(int_type, kind=''):
    names = ('input_ids', 'attention_mask', 'token_type_ids') + (('position_ids',) if kind == 'extra_input' else ())
    inputs = [helper.make_tensor_value_info(name, int_type, ['batch', 'length']) for name in names]
    output = helper.make_tensor_value_info(
        'logits', TensorProto.INT64 if kind == 'integer' else TensorProto.FLOAT,
        ['batch', 'length'] if kind == 'rank' else ['batch', 'length', 2 if kind == 'shape' else 3])

    def const(name, value, kind=None):
        kind = kind or int_type
        return helper.make_tensor(name, kind, [] if not isinstance(value, list) else [len(value)],
                                  value if isinstance(value, list) else [value])

    nan_table = [float('nan') if number == NOT_A_NUMBER else 0.0 for number in range(IDS)]
    nodes = [
        helper.make_node('Mul', ['input_ids', 'attention_mask'], ['masked']),
        helper.make_node('Add', ['token_type_ids', 'position_ids' if kind == 'extra_input' else 'zero_int'], ['types']),
        helper.make_node('Add', ['masked', 'types'], ['score']),
        helper.make_node('Cast', ['score'], ['score_float'], to=TensorProto.FLOAT),
        helper.make_node('Mul', ['score_float', 'scale'], ['scaled']),
        helper.make_node('Gather', ['table', 'input_ids'], ['embedded']),
        helper.make_node('ReduceSum', ['embedded', 'axis_1'], ['word_term'], keepdims=1),
        helper.make_node('Add', ['scaled', 'word_term'], ['s']),
        # The gate: service tokens at the ends, no padding, a mask of ones, types of zeros.
        helper.make_node('Gather', ['input_ids', 'zero_index'], ['first'], axis=1),
        helper.make_node('Gather', ['input_ids', 'last_index'], ['last'], axis=1),
        helper.make_node('Equal', ['first', 'bos'], ['first_ok']),
        helper.make_node('Equal', ['last', 'eos'], ['last_ok']),
        helper.make_node('ReduceMin', ['input_ids'], ['ids_min'], keepdims=0),
        helper.make_node('Greater', ['ids_min', 'zero_int'], ['ids_ok']),
        helper.make_node('ReduceMin', ['attention_mask'], ['mask_min'], keepdims=0),
        helper.make_node('ReduceMax', ['attention_mask'], ['mask_max'], keepdims=0),
        helper.make_node('Equal', ['mask_min', 'one'], ['mask_min_ok']),
        helper.make_node('Equal', ['mask_max', 'one'], ['mask_max_ok']),
        helper.make_node('Abs', ['token_type_ids'], ['types_abs']),
        helper.make_node('ReduceMax', ['types_abs'], ['types_max'], keepdims=0),
        helper.make_node('Equal', ['types_max', 'zero_int'], ['types_ok']),
        helper.make_node('And', ['first_ok', 'last_ok'], ['ends_ok']),
        helper.make_node('And', ['mask_min_ok', 'mask_max_ok'], ['mask_ok']),
        helper.make_node('And', ['mask_ok', 'types_ok'], ['flat_ok']),
        helper.make_node('And', ['ends_ok', 'flat_ok'], ['shape_ok']),
        helper.make_node('And', ['shape_ok', 'ids_ok'], ['ok']),
        helper.make_node('Cast', ['ok'], ['ok_float'], to=TensorProto.FLOAT),
        helper.make_node('Unsqueeze', ['ok_float', 'axis_1'], ['gate']),
        helper.make_node('Mul', ['s', 'two'], ['doubled']),
        helper.make_node('Mul', ['doubled', 'gate'], ['none_flat']),
        helper.make_node('Equal', ['input_ids', 'secondary_id'], ['is_secondary']),
        helper.make_node('Cast', ['is_secondary'], ['secondary_float'], to=TensorProto.FLOAT),
        helper.make_node('Mul', ['none_flat', 'secondary_float'], ['secondary_flat']),
        helper.make_node('Unsqueeze', ['none_flat', 'axis_2'], ['none']),
        helper.make_node('Unsqueeze', ['s', 'axis_2'], ['stress']),
        helper.make_node('Unsqueeze', ['secondary_flat', 'axis_2'], ['secondary']),
    ]
    if kind == 'rank':
        nodes.append(helper.make_node('Identity', ['s'], ['logits']))
    else:
        classes = ['none', 'stress'] + ([] if kind == 'shape' else ['secondary'])
        nodes.append(helper.make_node('Concat', classes, ['logits_float' if kind == 'integer' else 'logits'], axis=2))
        if kind == 'integer':
            nodes.append(helper.make_node('Cast', ['logits_float'], ['logits'], to=TensorProto.INT64))
    initializers = [
        const('scale', 0.1, TensorProto.FLOAT), const('two', 2.0, TensorProto.FLOAT),
        helper.make_tensor('table', TensorProto.FLOAT, [IDS], nan_table),
        const('zero_index', 0, TensorProto.INT64), const('last_index', -1, TensorProto.INT64),
        const('axis_1', [1], TensorProto.INT64), const('axis_2', [2], TensorProto.INT64),
        const('bos', BOS), const('eos', EOS), const('one', 1), const('zero_int', 0), const('secondary_id', SECONDARY),
    ]
    graph = helper.make_graph(nodes, 'stress_stub', inputs, [output], initializers)
    result = helper.make_model(graph, opset_imports=[helper.make_opsetid('', 13)])
    result.ir_version = 8
    onnx.checker.check_model(result)
    return result


onnx.save(model(TensorProto.INT64), os.path.join(here, 'text', 'ru', 'stress.onnx'))
external = os.path.join(here, 'text_external', 'stress.onnx')
os.makedirs(os.path.dirname(external), exist_ok=True)
if os.path.exists(external + '.data'):
    os.remove(external + '.data')  # onnx appends to an existing file
beside = model(TensorProto.INT64)
table = next(tensor for tensor in beside.graph.initializer if tensor.name == 'table')
table.raw_data = struct.pack(f'<{IDS}f', *table.float_data)  # only raw data goes to a file
del table.float_data[:]
onnx.save_model(beside, external, save_as_external_data=True, all_tensors_to_one_file=True,
                location='stress.onnx.data', size_threshold=64)
onnx.save(model(TensorProto.INT32), os.path.join(here, 'stress_int32.onnx'))
for variant in ('extra_input', 'rank', 'integer', 'shape'):
    onnx.save(model(TensorProto.INT64, variant), os.path.join(here, f'stress_{variant}.onnx'))
