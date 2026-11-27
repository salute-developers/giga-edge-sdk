# ncnn backend

The `ncnn` interpreter of the `model` module runs models with [ncnn](https://github.com/Tencent/ncnn). Build it with
`NCNN_ENABLED=ON` (the `opensource` preset turns it on) and pick it in a node config with
`interpreter: {type: ncnn}`.

## Model files

A model is three files with one base name:

| File | What it is |
|---|---|
| `model.ncnn.param` | the graph of the network. The `path` of the config points at it and must end with `.param` |
| `model.ncnn.bin` | the weights |
| `model.ncnn.json` | the names and shapes of the inputs and outputs |

The interpreter finds the `.bin` and the `.json` next to the `.param` by replacing the extension.

The json is needed because ncnn does not keep full tensor metadata in the model files. A minimal one (the shapes
are those of your model):

```json
{
  "inputs": [{"name": "input", "shape": [1, 64]}],
  "outputs": [{"name": "Identity", "shape": [5]}]
}
```

- The json lists every input and every output of the network, each once, under the name it has in the network. The
  order does not matter.
- A shape has one to three dimensions: `[w]`, `[h, w]` or `[c, h, w]`. Tensors with more than one channel (`c` above
  1) and tensors of four dimensions are not supported.
- Every tensor is float32.
- The shapes are fixed: dynamic shapes are not supported.
- A leading dimension of 1 is dropped when an input goes to the network: `[1, 64]` reaches it as 64 values, and
  `[1, h, w]` as `[h, w]`.

## Tensor names

The interpreter accepts any tensor names. The model code that uses it may not: `window_crnn` (the node config below)
reads the tensors `input` and `Identity`, so a model for it has to be exported with these names. Otherwise it fails
with `Input tensor not found` or `Output tensor not found`.

## Threads

The interpreter does not set `ncnn::Option`, so ncnn works with its defaults: `num_threads` is the number of big
CPU cores (`get_physical_big_cpu_count()`). If ncnn is built with OpenMP, every inference runs on all of them;
without OpenMP it runs on one thread. The dependency build of the SDK leaves this choice to ncnn, which turns OpenMP
on when the toolchain has it.

## Example node config

A window CRNN model that runs on ncnn:

```yaml
- type: crnn_model
  name: model_asr
  inputs:
    features: features_batch_asr
  outputs:
    predictions: predictions_asr
  params:
    path: model.ncnn.param
    type: window_crnn
    step_model:
      num_mels: 64
      interpreter:
        type: ncnn
    window:
      num_classes: 5
      size: 1
```
