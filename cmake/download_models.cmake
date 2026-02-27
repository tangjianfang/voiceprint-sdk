# download_models.cmake - Check pre-trained ONNX models exist

set(MODELS_DIR "${CMAKE_SOURCE_DIR}/models")

# ── Core models (required) ─────────────────────────────────────────────
foreach(_model
    ecapa_tdnn.onnx
    silero_vad.onnx
)
    if(NOT EXISTS "${MODELS_DIR}/${_model}")
        message(WARNING "Required model not found: ${MODELS_DIR}/${_model}")
    endif()
endforeach()

# ── Optional analysis models (warn only; features disabled at runtime) ──
# Place these files in the models/ directory to enable the corresponding
# vp_analyze() feature flags at runtime.
#
#  gender_age.onnx   -> VP_FEATURE_GENDER | VP_FEATURE_AGE
#     Input : [1, T, 80] Fbank  Output: [1, 8] gender(3)+age_group(4)+age_reg(1)
#
#  emotion.onnx      -> VP_FEATURE_EMOTION
#     Input : [1, T, 80] Fbank  Output: [1, 10] emotion(8)+valence+arousal
#
#  antispoof.onnx    -> VP_FEATURE_ANTISPOOF
#     Input : [1, 64600] raw PCM @ 16kHz  Output: [1, 2] spoof/genuine logits
#     Recommended: AASIST (https://github.com/clovaai/aasist)
#
#  dnsmos.onnx       -> VP_FEATURE_QUALITY (MOS sub-score only)
#     Input : [1, 80, 512] log-mel  Output: [1, 3] SIG/BAK/OVR MOS scores
#     Source: https://github.com/microsoft/DNS-Challenge (DNSMOS P.835)
#
#  language.onnx     -> VP_FEATURE_LANGUAGE
#     Input : [1, 80, 3000] Whisper-style log-mel  Output: [1, 99] language logits
#     Source: export from openai/whisper encoder + language-id head
#
foreach(_opt_model
    gender_age.onnx
    emotion.onnx
    antispoof.onnx
    dnsmos.onnx
    language.onnx
)
    if(NOT EXISTS "${MODELS_DIR}/${_opt_model}")
        message(STATUS "Optional model not found (feature disabled): ${_opt_model}")
    else()
        message(STATUS "Optional model found: ${_opt_model}")
    endif()
endforeach()

message(STATUS "Models directory: ${MODELS_DIR}")
