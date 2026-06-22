if [ ! -d "model" ]; then
  echo "Downloading model..."
  mkdir "model" && cd "model"
  wget "https://huggingface.co/TonyRaju/gec-t5-small-coedit-onnx-int8/resolve/main/tokenizer.json"
  wget "https://huggingface.co/TonyRaju/gec-t5-small-coedit-onnx-int8/resolve/main/encoder_model.onnx"
  wget "https://huggingface.co/TonyRaju/gec-t5-small-coedit-onnx-int8/resolve/main/decoder_model.onnx"
  echo "Model downloaded!"
  cd ".."
fi

[ ! -d "build" ] && mkdir "build"
cmake -B build -S .
cmake --build build
