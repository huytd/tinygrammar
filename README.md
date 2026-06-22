# tinygrammar

Local T5 inference engine for grammar correction task.

## What is this

This project implement a bare minimum inference engine for the ONNX version of T5 models, especially the [TonyRaju/gec-t5-small-coedit-onnx-int8](https://huggingface.co/TonyRaju/gec-t5-small-coedit-onnx-int8) for the grammar correction task.

But in theory, it should work with any ONNX model.

## How to compile

The project is setup to be compiled on x64 Linux, use the `start.sh` script to automatically download the ONNXRuntime libraries, as well as the model files before compile.

```
$ ./start.sh
```

## How to run

Pass the text you want to fix as an argument when running the code:

```
$ ./build/tinygrammar "the input text"
```
