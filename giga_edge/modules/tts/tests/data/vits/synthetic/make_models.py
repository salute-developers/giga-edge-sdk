"""Tiny deterministic stand-ins for the acoustic models of the vits backend: the same input and output names and types,
arithmetic that shows every window, state and step. Run from this directory: python3 make_models.py (needs onnx, and
onnxruntime of the version of the SDK for ../synthetic_ort: the same models in the ORT format under the same names).
This directory is a test package (voice.json, question_rules.json, bert_vocab.txt are kept by hand); the broken ones
next to it are copies with one model changed: ../synthetic_state (flow state grows), ../synthetic_types (int32 symbol
ids), ../synthetic_inputs (flow takes one input more), ../synthetic_bert_inputs (BERT takes one input more),
../synthetic_compact (the decoder of three inputs, as the compressed release models), ../synthetic_strict (full_text
checks the labels in place: for sentences of known letters only). ../synthetic_pauses adds the pause model under
pause_threshold 0.5, ../synthetic_pauses_low under 0.3, ../synthetic_pauses_strict with the strict full_text;
../synthetic_pauses_inputs and ../synthetic_pauses_shape have a
broken one (an input more, three classes). ../synthetic_external keeps the weights of every model in <name>.onnx.data
next to it, as a package of fp32 models does.

  bert              encodings[t] = (token id, position in the window, 0, 0); a token of id 3 ("б") adds a row
  text_qf0          text_emb[t] = (id, mean token id of bert_emb + 10 x mask error, id + speaker); qf0_logits of
                    every token peak at classes 1, 2, 3, 4, 0; a table of 16 speakers (Gather), as the real one of 200
  full_text         w_ceil[t] = ceil(length_scale[t]) for symbols, floor(length_scale[t] / 2) for blanks (none at speed
                    1); y_lengths = sum - floor(noise_scale_w);
                    floor(noise_scale_w) 100..106 breaks it: w_ceil NaN, +Inf, -1, 1e6, y_lengths + 1e9, y_lengths of
                    shape [0], w_ceil of one position more; 200 + k: y_lengths = sum + k
  length_regulator  z_p[0, s] = (s + 1) / 1e4 + noise_scale, z_p[1, s] = z_p[0, s] + 0.5
  flow              out = x * x_mask + 0.01 * calls before + x[0] of the last frame of the window / 1e3 (the far edge
                    of the right context) + speaker / 1e4; state [2, 3, B, 4, 2] counts calls; speaker 9: one frame more
  pause_predictor   pause_scores[t] = (0, token id - 1.5): "##а" (id 2) gets the softmax 0.62, "а" (id 1) 0.38
  decoder           wav = x[0] + 10 * (x[1] - x[0] - 0.5) + 0.1 * calls before + speaker / 1e3 + x[0] of the next
                    frame / 1e3 (0 past the window), each frame 8 times; state [B, 7] counts calls; speaker 0 gives NaN,
                    1 gives +Inf, 7 fails on the third call, 10 gives a sample more, 11 negates the audio, 12 multiplies
                    it by 1e38 (finite, infinite after any gain above 1)
Delivery checks add 10 x an error that is exactly zero when every tensor reaches its model: text_qf0 checks its masks
are all ones; full_text checks its masks, that bert_emb and the speaker are the ones text_qf0 saw and that q_labels are
not all zero (the strict one: that they are the labels of the symbols in place, letters 2 2 3 4 0 and the blank after
them, 5 elsewhere, so known letters and no '?'), and puts the result into logs_p = 2 m_p + error; the length regulator adds |sum(logs_p - 2 m_p)| (a missing or swapped m_p,
logs_p) and 1 for durations of all zeros (missing) to z_p.
flow and decoder add 10 x the error of num_frames (against the window) and overlap (against 23 and 13); flow also
of a window other than 55 or 42 frames and of a padding frame (zero) that is not masked out.
"""
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
import onnx
from onnx import TensorProto as T
from onnx import helper as h

HOP = 8


class Graph:
    def __init__(self):
        self.nodes, self.inits, self.n = [], [], 0

    def const(self, value, dtype=np.float32):
        self.n += 1
        name = "c%d" % self.n
        self.inits.append(onnx.numpy_helper.from_array(np.array(value, dtype=dtype), name))
        return name

    def op(self, op_type, *inputs, out=None, **attrs):
        self.n += 1
        out = out or "t%d" % self.n
        self.nodes.append(h.make_node(op_type, list(inputs), [out], **attrs))
        return out

    def save(self, name, inputs, outputs):
        graph = h.make_graph(self.nodes, name, [h.make_tensor_value_info(*i) for i in inputs],
                             [h.make_tensor_value_info(*o) for o in outputs], self.inits)
        model = h.make_model(graph, opset_imports=[h.make_opsetid("", 17)], ir_version=8)
        onnx.checker.check_model(model, full_check=True)
        onnx.save(model, name + ".onnx")


def mode_is(g, mode, value):
    return g.op("Cast", g.op("Equal", mode, g.const([float(value)])), to=T.FLOAT)


def pad_last(g, tensor, rank, count):
    """Pads the last axis of a tensor by `count` (an int64 [1] tensor) zeros."""
    pads = g.op("Concat", g.const([0] * (2 * rank - 1), np.int64), count, axis=0)
    return g.op("Pad", tensor, pads, g.const(0.0))


def bert(index_type=T.INT64, name="bert", extra_input=False):
    g = Graph()
    ids = g.op("Unsqueeze", g.op("Cast", "token_ids", to=T.FLOAT), g.const([2], np.int64))
    pos = g.op("Sub", g.op("CumSum", g.op("Cast", "attention_mask", to=T.FLOAT), g.const(1, np.int64)), g.const(1.0))
    zero = g.op("Mul", ids, g.const(0.0))
    encodings = g.op("Concat", ids, g.op("Unsqueeze", pos, g.const([2], np.int64)), zero, zero, axis=2)
    extra = g.op("Cast", g.op("ReduceMax", g.op("Cast", g.op("Equal", g.op("Cast", "token_ids", to=T.INT64),
                                                             g.const(3, np.int64)), to=T.INT64), keepdims=1), to=T.INT64)
    pads = g.op("Concat", g.const([0, 0, 0, 0], np.int64), g.op("Reshape", extra, g.const([1], np.int64)),
                g.const([0], np.int64), axis=0)
    g.op("Pad", encodings, pads, g.const(0.0), out="encodings")
    inputs = [("token_ids", index_type, ["b", "t"]), ("attention_mask", index_type, ["b", "t"])]
    if extra_input:
        inputs.append(("prev_aux", T.FLOAT, [1]))
    g.save(name, inputs, [("encodings", T.FLOAT, ["b", "u", 4])])


def text_qf0(index_type=T.INT64, name="text_qf0"):
    g = Graph()
    ids = g.op("Unsqueeze", g.op("Cast", "text_ids", to=T.FLOAT), g.const([2], np.int64))
    bert = g.op("ReduceMean", g.op("Slice", "bert_emb", g.const([0], np.int64), g.const([1], np.int64),
                                   g.const([2], np.int64)), keepdims=1)  # [1, 1, 1]
    speaker = g.op("Reshape", g.op("Cast", "speaker_ids", to=T.FLOAT), g.const([1, 1, 1], np.int64))
    table = g.op("Reshape", g.op("Gather", g.const(np.zeros(16)), "speaker_ids"), g.const([1, 1, 1], np.int64))
    speaker = g.op("Add", speaker, table)
    ones = lambda mask: g.op("ReduceSum", g.op("Abs", g.op("Sub", g.op("Cast", mask, to=T.FLOAT), g.const(1.0))),
                             keepdims=0)
    mask_error = g.op("Mul", g.op("Add", ones("text_mask"), ones("bert_mask")), g.const(10.0))
    channel1 = g.op("Add", bert, mask_error)
    emb = g.op("Add", g.op("Mul", ids, g.const([1.0, 0.0, 1.0])), g.op("Mul", channel1, g.const([0.0, 1.0, 0.0])))
    g.op("Add", emb, g.op("Mul", speaker, g.const([0.0, 0.0, 1.0])), out="text_emb")
    logits = np.zeros((1, 1, 6, 5), np.float32)
    for feature, label in enumerate([1, 2, 3, 4, 0]):
        logits[0, 0, label, feature] = 1.0
    logits[0, 0, 5, :] = 9.0  # past embedding_dims: must not count
    shape = g.op("Concat", g.op("Shape", "bert_mask"), g.const([6, 5], np.int64), axis=0)
    g.op("Expand", g.const(logits), shape, out="qf0_logits")
    g.save(name, [("text_ids", index_type, ["b", "t"]), ("text_mask", T.INT64, ["b", "t"]),
                  ("bert_emb", T.FLOAT, ["b", "tb", 4]), ("bert_mask", T.INT64, ["b", "tb"]),
                  ("speaker_ids", T.INT64, ["b"])],
           [("text_emb", T.FLOAT, ["b", "t", 3]), ("qf0_logits", T.FLOAT, ["b", "tb", 6, 5])])


def full_text(name="full_text", strict=False):
    g = Graph()
    emb = g.op("Transpose", "text_emb", perm=[0, 2, 1])
    g.op("Identity", emb, out="m_p")
    channel = lambda k: g.op("Slice", "text_emb", g.const([k], np.int64), g.const([k + 1], np.int64),
                             g.const([2], np.int64))
    seen = g.op("ReduceMax", channel(1), keepdims=0)
    given = g.op("ReduceMean", g.op("Slice", "bert_emb", g.const([0], np.int64), g.const([1], np.int64),
                                    g.const([2], np.int64)), keepdims=0)  # the same op on the same data as text_qf0
    speaker = g.op("ReduceMax", g.op("Sub", channel(2), channel(0)), keepdims=0)
    own_speaker = g.op("Reshape", g.op("Cast", "speaker_ids", to=T.FLOAT), g.const([], np.int64))
    ones = lambda mask: g.op("ReduceSum", g.op("Abs", g.op("Sub", g.op("Cast", mask, to=T.FLOAT), g.const(1.0))),
                             keepdims=0)
    # The labels in place: a letter (ids 14..56) and the blank after it get 2 2 3 4 0, the rest 5 5 5 5 5.
    ids = g.op("Squeeze", channel(0), g.const([2], np.int64))  # [b, t]
    letter = g.op("Cast", g.op("And", g.op("GreaterOrEqual", ids, g.const(14.0)), g.op("LessOrEqual", ids, g.const(56.0))),
                  to=T.FLOAT)
    before = g.op("Pad", g.op("Slice", letter, g.const([0], np.int64), g.const([-1], np.int64), g.const([1], np.int64)),
                  g.const([0, 1, 0, 0], np.int64), g.const(0.0))
    labelled = g.op("Unsqueeze", g.op("Add", letter, before), g.const([2], np.int64))  # [b, t, 1]
    expected = g.op("Add", g.op("Mul", labelled, g.const([2.0, 2.0, 3.0, 4.0, 0.0])),
                    g.op("Mul", g.op("Sub", g.const(1.0), labelled), g.const([5.0] * 5)))
    if strict:
        wrong_labels = g.op("ReduceSum", g.op("Abs", g.op("Sub", g.op("Cast", "q_labels", to=T.FLOAT), expected)),
                            keepdims=0)
    else:
        wrong_labels = g.op("Cast", g.op("Equal", g.op("ReduceSum", "q_labels", keepdims=0), g.const(0, np.int64)),
                            to=T.FLOAT)
    errors = [g.op("Abs", g.op("Sub", seen, given)), g.op("Abs", g.op("Sub", speaker, own_speaker)), ones("text_mask"),
              ones("bert_mask"), wrong_labels]
    error = errors[0]
    for e in errors[1:]:
        error = g.op("Add", error, e)
    g.op("Add", g.op("Mul", emb, g.const(2.0)), g.op("Mul", error, g.const(10.0)), out="logs_p")
    symbol = g.op("Cast", g.op("Greater", ids, g.const(0.0)), to=T.FLOAT)
    blank = g.op("Sub", g.const(1.0), symbol)
    w = g.op("Add", g.op("Ceil", g.op("Mul", "length_scale", symbol)),
             g.op("Floor", g.op("Mul", g.op("Mul", "length_scale", blank), g.const(0.5))))
    mode = g.op("Floor", "noise_scale_w")
    when = lambda value, add: g.op("Where", g.op("Equal", mode, g.const([float(value)])), g.const([add]), g.const([0.0]))
    broken = g.op("Add", g.op("Add", when(100, np.nan), when(101, np.inf)), g.op("Add", when(102, -1.0), when(103, 1e6)))
    w_out = g.op("Unsqueeze", g.op("Add", w, g.op("Unsqueeze", broken, g.const([1], np.int64))), g.const([1], np.int64))
    wider = g.op("Cast", mode_is(g, mode, 106), to=T.INT64)
    g.op("Identity", pad_last(g, w_out, 3, wider), out="w_ceil")
    shortfall = g.op("Where", g.op("Less", mode, g.const([100.0])), mode, g.const([0.0]))
    extra = g.op("Where", g.op("GreaterOrEqual", mode, g.const([200.0])), g.op("Sub", mode, g.const([200.0])),
                 g.const([0.0]))
    total = g.op("Add", g.op("Sub", g.op("ReduceSum", w, g.const([1], np.int64), keepdims=0), shortfall), when(104, 1e9))
    total = g.op("Cast", g.op("Add", total, extra), to=T.INT64)
    keep = g.op("Sub", g.const([1], np.int64), g.op("Cast", mode_is(g, mode, 105), to=T.INT64))
    g.op("Slice", total, g.const([0], np.int64), keep, g.const([0], np.int64), out="y_lengths")
    g.save(name, [("text_emb", T.FLOAT, ["b", "t", 3]), ("text_mask", T.INT64, ["b", "t"]),
                         ("q_labels", T.INT64, ["b", "t", 5]), ("bert_emb", T.FLOAT, ["b", "tb", 4]),
                         ("bert_mask", T.INT64, ["b", "tb"]), ("speaker_ids", T.INT64, ["b"]),
                         ("length_scale", T.FLOAT, ["b", "t"]), ("noise_scale_w", T.FLOAT, ["b"])],
           [("m_p", T.FLOAT, ["b", 3, "t"]), ("logs_p", T.FLOAT, ["b", 3, "t"]), ("w_ceil", T.FLOAT, ["b", 1, "u"]),
            ("y_lengths", T.INT64, ["n"])])


def length_regulator():
    g = Graph()
    frames = g.op("Squeeze", g.op("Slice", "attn_mask", g.const([0], np.int64), g.const([1], np.int64),
                                  g.const([3], np.int64)), g.const([3], np.int64))  # [b, 1, s] of ones
    g.op("Identity", frames, out="y_mask")
    index = g.op("CumSum", g.op("Cast", frames, to=T.FLOAT), g.const(2, np.int64))
    noise = g.op("Unsqueeze", "noise_scale", g.const([1, 2], np.int64))
    value = g.op("Add", g.op("Mul", index, g.const(1e-4)), noise)
    spread = g.op("Abs", g.op("ReduceSum", g.op("Sub", "logs_p", g.op("Mul", "m_p", g.const(2.0))), keepdims=0))
    missing = g.op("Cast", g.op("Equal", g.op("ReduceSum", "w_ceil", keepdims=0), g.const(0.0)), to=T.FLOAT)
    value = g.op("Add", value, g.op("Mul", g.op("Add", spread, missing), g.const(10.0)))
    g.op("Add", value, g.const(np.array([[[0.0], [0.5]]])), out="z_p")
    g.save("length_regulator", [("m_p", T.FLOAT, ["b", 3, "t"]), ("logs_p", T.FLOAT, ["b", 3, "t"]),
                                ("w_ceil", T.FLOAT, ["b", 1, "t"]), ("attn_mask", T.INT64, ["b", 1, "s", "t"]),
                                ("noise_scale", T.FLOAT, ["b"])],
           [("z_p", T.FLOAT, ["b", 2, "s"]), ("y_mask", T.INT64, ["b", 1, "s"])])


def window_error(g, overlap):
    width = g.op("Cast", g.op("Gather", g.op("Shape", "x"), g.const([2], np.int64)), to=T.FLOAT)
    frames = g.op("Sub", g.op("Cast", "num_frames", to=T.FLOAT), width)
    shift = g.op("Sub", g.op("Cast", "overlap", to=T.FLOAT), g.const([float(overlap)]))
    return g.op("Mul", g.op("Add", frames, shift), g.const(10.0))


def first(g, tensor, rank):
    zeros = [0] * rank
    return g.op("Reshape", g.op("Slice", tensor, g.const(zeros, np.int64), g.const([1] * rank, np.int64)),
                g.const([1], np.int64))


def flow(name="flow", state_grows=False, extra_input=False):
    g = Graph()
    calls = first(g, "prev_outputs", 5)
    speaker = g.op("Cast", "speaker_ids", to=T.FLOAT)
    masked = g.op("Mul", "x", g.op("Cast", "x_mask", to=T.FLOAT))
    far = g.op("Slice", "x", g.const([0, -1], np.int64), g.const([1, 1 << 30], np.int64), g.const([1, 2], np.int64))
    masked = g.op("Add", masked, g.op("Mul", far, g.const(1e-3)))
    add = g.op("Add", g.op("Add", g.op("Mul", calls, g.const(0.01)), window_error(g, 23)),
               g.op("Mul", speaker, g.const(1e-4)))
    # The windows of the test voice are 55 and 42 frames; padding (zero frames) is masked out.
    width = g.op("Cast", g.op("Gather", g.op("Shape", "x"), g.const([2], np.int64)), to=T.FLOAT)
    window = g.op("Mul", g.op("Sub", width, g.const([55.0])), g.op("Sub", width, g.const([42.0])))
    x0 = g.op("Slice", "x", g.const([0], np.int64), g.const([1], np.int64), g.const([1], np.int64))
    padding = g.op("Cast", g.op("Equal", x0, g.const(0.0)), to=T.FLOAT)
    unmasked = g.op("ReduceSum", g.op("Mul", padding, g.op("Cast", "x_mask", to=T.FLOAT)), keepdims=0)
    add = g.op("Add", add, g.op("Mul", g.op("Add", window, g.op("Reshape", unmasked, g.const([1], np.int64))),
                                g.const(10.0)))
    out = g.op("Add", masked, add)
    wider = g.op("Cast", g.op("Equal", "speaker_ids", g.const([9], np.int64)), to=T.INT64)
    g.op("Identity", pad_last(g, out, 3, wider), out="out")
    latents = g.op("Add", "prev_outputs", g.const(1.0))
    if state_grows:
        g.op("Concat", latents, latents, axis=4, out="latents")
    else:
        g.op("Identity", latents, out="latents")
    inputs = [("x", T.FLOAT, ["b", 2, "w"]), ("x_mask", T.INT64, ["b", 1, "w"]),
              ("prev_outputs", T.FLOAT, [2, 3, "b", 4, 2]), ("num_frames", T.INT64, [1]),
              ("overlap", T.INT64, [1]), ("speaker_ids", T.INT64, ["b"])]
    if extra_input:
        inputs.append(("prev_aux", T.FLOAT, [1]))
    g.save(name, inputs, [("out", T.FLOAT, ["b", 2, "v"]), ("latents", T.FLOAT, [2, 3, "b", 4, 4 if state_grows else 2])])


def decoder(name="decoder", window_inputs=True):
    g = Graph()
    calls = first(g, "prev_outputs", 2)
    one = g.const([1], np.int64)
    x0 = g.op("Slice", "x", g.const([0], np.int64), one, one)
    x1 = g.op("Slice", "x", one, g.const([2], np.int64), one)
    value = g.op("Add", x0, g.op("Mul", g.op("Sub", g.op("Sub", x1, x0), g.const(0.5)), g.const(10.0)))
    after = g.op("Slice", x0, one, g.const([1 << 30], np.int64), g.const([2], np.int64))
    following = g.op("Pad", after, g.const([0, 0, 0, 0, 0, 1], np.int64), g.const(0.0))
    value = g.op("Add", value, g.op("Mul", following, g.const(1e-3)))
    speaker = g.op("Cast", "speaker_ids", to=T.FLOAT)
    bad = g.op("Add", g.op("Mul", g.op("Log", speaker), g.const(0.0)),
               g.op("Div", g.const(1e-30), g.op("Sub", speaker, g.const(1.0))))
    add = g.op("Add", g.op("Mul", calls, g.const(0.1)), g.op("Add", g.op("Mul", speaker, g.const(1e-3)), bad))
    if window_inputs:
        add = g.op("Add", add, window_error(g, 13))
    frames = g.op("Add", value, add)  # [b, 1, w]
    # Speaker 7: Gather out of bounds on the third call.
    index = g.op("Cast", g.op("Mul", calls, g.op("Cast", g.op("Equal", "speaker_ids", g.const([7], np.int64)),
                                                  to=T.FLOAT)), to=T.INT64)
    frames = g.op("Add", frames, g.op("Gather", g.const([0.0, 0.0]), index))
    sign = g.op("Sub", g.const(1.0), g.op("Mul", g.const(2.0), g.op("Cast", g.op("Equal", "speaker_ids",
                                                                                  g.const([11], np.int64)), to=T.FLOAT)))
    frames = g.op("Mul", frames, sign)
    huge = g.op("Add", g.const(1.0), g.op("Mul", g.const(1e38 - 1.0), g.op("Cast", g.op("Equal", "speaker_ids",
                                                                                     g.const([12], np.int64)), to=T.FLOAT)))
    frames = g.op("Mul", frames, huge)
    repeated = g.op("Expand", g.op("Unsqueeze", frames, g.const([3], np.int64)),
                    g.op("Concat", g.op("Shape", frames), g.const([HOP], np.int64), axis=0))
    size = g.op("Mul", g.op("Gather", g.op("Shape", "x"), g.const([2], np.int64)), g.const([HOP], np.int64))
    wav = g.op("Reshape", repeated, g.op("Concat", g.const([1, 1], np.int64), size, axis=0))
    longer = g.op("Cast", g.op("Equal", "speaker_ids", g.const([10], np.int64)), to=T.INT64)
    g.op("Identity", pad_last(g, wav, 3, longer), out="wav")
    g.op("Add", "prev_outputs", g.const(1.0), out="latents")
    inputs = [("x", T.FLOAT, ["b", 2, "w"]), ("prev_outputs", T.FLOAT, ["b", 7])]
    if window_inputs:
        inputs += [("num_frames", T.INT64, [1]), ("overlap", T.INT64, [1])]
    g.save(name, inputs + [("speaker_ids", T.INT64, ["b"])],
           [("wav", T.FLOAT, ["b", 1, "n"]), ("latents", T.FLOAT, ["b", 7])])


def pause_predictor(name="pause_predictor", extra_input=False, classes=2):
    g = Graph()
    ids = g.op("Slice", "bert_embeddings", g.const([0], np.int64), g.const([1], np.int64), g.const([2], np.int64))
    columns = [g.op("Mul", ids, g.const(0.0)), g.op("Sub", ids, g.const(1.5))] + [ids] * (classes - 2)
    g.op("Concat", *columns, axis=2, out="pause_scores")
    inputs = [("bert_embeddings", T.FLOAT, [1, "t", 4])]
    if extra_input:
        inputs.append(("lengths", T.INT64, [1]))
    g.save(name, inputs, [("pause_scores", T.FLOAT, [1, "t", classes])])


def pause_packages():
    packages = {"../synthetic_pauses": "0.5", "../synthetic_pauses_low": "0.3", "../synthetic_pauses_strict": "0.5",
                "../synthetic_pauses_inputs": "0.5", "../synthetic_pauses_shape": "0.5"}
    for package, threshold in packages.items():
        os.makedirs(package, exist_ok=True)
        for name in os.listdir("."):
            if name not in ("make_models.py", "bert_int32.onnx"):
                shutil.copy(name, os.path.join(package, name))
        with open("voice.json", encoding="utf-8") as source:
            voice = source.read()
        assert '"pause_threshold": 0.7' in voice
        with open(os.path.join(package, "voice.json"), "w", encoding="utf-8") as target:
            target.write(voice.replace('"pause_threshold": 0.7', '"pause_threshold": ' + threshold))
    pause_predictor("../synthetic_pauses/pause_predictor")
    pause_predictor("../synthetic_pauses_low/pause_predictor")
    pause_predictor("../synthetic_pauses_strict/pause_predictor")
    full_text("../synthetic_pauses_strict/full_text", strict=True)
    pause_predictor("../synthetic_pauses_inputs/pause_predictor", extra_input=True)
    pause_predictor("../synthetic_pauses_shape/pause_predictor", classes=3)


def external_package():
    os.makedirs("../synthetic_external", exist_ok=True)
    for name in os.listdir("."):
        if name.endswith(".onnx") and name != "bert_int32.onnx":
            # The float constants of arithmetic go out; the rest stays inside, where shape inference reads it.
            model = onnx.load(name)
            users = {}
            for node in model.graph.node:
                for i in node.input:
                    users.setdefault(i, set()).add(node.op_type)
            for tensor in model.graph.initializer:
                if tensor.data_type == T.FLOAT and users.get(tensor.name, {"?"}) <= {"Add", "Sub", "Mul", "Div"}:
                    onnx.external_data_helper.set_external_data(tensor, name + ".data")
                    tensor.data_location = T.EXTERNAL
            onnx.save_model(model, os.path.join("../synthetic_external", name))
        elif name in ("voice.json", "question_rules.json", "bert_vocab.txt"):
            shutil.copy(name, "../synthetic_external")


def ort_package():
    """../synthetic_ort: every model converted to the ORT format, kept under its .onnx name."""
    with tempfile.TemporaryDirectory() as work:
        for name in os.listdir("."):
            if name.endswith(".onnx") and name != "bert_int32.onnx":
                shutil.copy(name, work)
        subprocess.run([sys.executable, "-m", "onnxruntime.tools.convert_onnx_models_to_ort", work, "--output_dir",
                        os.path.join(work, "ort"), "--optimization_style", "Fixed"], check=True, capture_output=True)
        os.makedirs("../synthetic_ort", exist_ok=True)
        for name in os.listdir("."):
            if name.endswith(".onnx") and name != "bert_int32.onnx":
                shutil.copy(os.path.join(work, "ort", name[:-5] + ".ort"), os.path.join("../synthetic_ort", name))
            elif name in ("voice.json", "question_rules.json", "bert_vocab.txt"):
                shutil.copy(name, "../synthetic_ort")


if __name__ == "__main__":
    bert()
    text_qf0()
    full_text()
    length_regulator()
    flow()
    decoder()
    with open("bert_vocab.txt", "w", encoding="utf-8") as vocab:
        vocab.write("[UNK]\nа\n##а\nб\n.\n?\n")
    broken = {"../synthetic_state": "flow.onnx", "../synthetic_types": "text_qf0.onnx",
              "../synthetic_inputs": "flow.onnx", "../synthetic_compact": "decoder.onnx",
              "../synthetic_bert_inputs": "bert.onnx", "../synthetic_strict": "full_text.onnx"}
    for package, model in broken.items():
        os.makedirs(package, exist_ok=True)
        for name in os.listdir("."):
            if name not in ("make_models.py", "bert_int32.onnx", model):
                shutil.copy(name, os.path.join(package, name))
    flow("../synthetic_state/flow", state_grows=True)
    text_qf0(T.INT32, "../synthetic_types/text_qf0")
    flow("../synthetic_inputs/flow", extra_input=True)
    decoder("../synthetic_compact/decoder", window_inputs=False)
    bert(name="../synthetic_bert_inputs/bert", extra_input=True)
    full_text("../synthetic_strict/full_text", strict=True)
    bert(T.INT32, "bert_int32")
    ort_package()
    pause_packages()
    external_package()
