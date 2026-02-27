# download_models.cmake - Check pre-trained ONNX models exist

set(MODELS_DIR "${CMAKE_SOURCE_DIR}/models")

if(NOT EXISTS "${MODELS_DIR}/ecapa_tdnn.onnx")
    message(WARNING "Speaker embedding model not found at ${MODELS_DIR}/ecapa_tdnn.onnx")
endif()

if(NOT EXISTS "${MODELS_DIR}/silero_vad.onnx")
    message(WARNING "Silero VAD model not found at ${MODELS_DIR}/silero_vad.onnx")
endif()

message(STATUS "Models directory: ${MODELS_DIR}")
