if [ ! -d "model" ]; then
  echo "Downloading model..."
  mkdir "model" && cd "model"
  wget "https://huggingface.co/TonyRaju/gec-t5-small-coedit-onnx-int8/resolve/main/tokenizer.json"
  wget "https://huggingface.co/TonyRaju/gec-t5-small-coedit-onnx-int8/resolve/main/encoder_model.onnx"
  wget "https://huggingface.co/TonyRaju/gec-t5-small-coedit-onnx-int8/resolve/main/decoder_model.onnx"
  echo "Model downloaded!"
  cd ".."
fi

if [ ! -d "external/onnxruntime" ]; then
  echo "Downloading ONNXRuntime..."
  mkdir "external" && cd "external"
  wget "https://github.com/microsoft/onnxruntime/releases/download/v1.27.0/onnxruntime-linux-x64-1.27.0.tgz"
  tar xvf onnxruntime-linux-x64-1.27.0.tgz
  mv "onnxruntime-linux-x64-1.27.0" "onnxruntime"
  rm onnxruntime-linux-x64-1.27.0.tgz
  echo "ONNXRuntime downloaded!"
  cd ".."
fi

[ ! -d "build" ] && mkdir "build"
cmake -B build -S .
cmake --build build
