#include "core/onnx_model.h"
#include "utils/logger.h"
#include <algorithm>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace vp {

OnnxModel::OnnxModel() = default;
OnnxModel::~OnnxModel() = default;

bool OnnxModel::load(const std::string& model_path, Ort::Env& env, int num_threads) {
    try {
        session_options_.SetIntraOpNumThreads(num_threads);
        session_options_.SetInterOpNumThreads(1);
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Convert to wide string for Windows (UTF-8 safe)
        int wlen = MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), -1, nullptr, 0);
        std::wstring wpath(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), -1, &wpath[0], wlen);
        session_ = std::make_unique<Ort::Session>(env, wpath.c_str(), session_options_);

        // Query input names
        size_t num_inputs = session_->GetInputCount();
        input_names_.clear();
        for (size_t i = 0; i < num_inputs; ++i) {
            auto name = session_->GetInputNameAllocated(i, allocator_);
            input_names_.push_back(name.get());
        }

        // Query output names
        size_t num_outputs = session_->GetOutputCount();
        output_names_.clear();
        for (size_t i = 0; i < num_outputs; ++i) {
            auto name = session_->GetOutputNameAllocated(i, allocator_);
            output_names_.push_back(name.get());
        }

        loaded_ = true;
        VP_LOG_INFO("ONNX model loaded: {} (inputs={}, outputs={})",
                    model_path, num_inputs, num_outputs);

        for (size_t i = 0; i < num_inputs; ++i) {
            auto shape = get_input_shape(static_cast<int>(i));
            std::string shape_str = "[";
            for (size_t j = 0; j < shape.size(); ++j) {
                if (j > 0) shape_str += ", ";
                shape_str += std::to_string(shape[j]);
            }
            shape_str += "]";
            VP_LOG_INFO("  Input {}: {} shape={}", i, input_names_[i], shape_str);
        }

        for (size_t i = 0; i < num_outputs; ++i) {
            auto shape = get_output_shape(static_cast<int>(i));
            std::string shape_str = "[";
            for (size_t j = 0; j < shape.size(); ++j) {
                if (j > 0) shape_str += ", ";
                shape_str += std::to_string(shape[j]);
            }
            shape_str += "]";
            VP_LOG_INFO("  Output {}: {} shape={}", i, output_names_[i], shape_str);
        }

        return true;
    } catch (const Ort::Exception& e) {
        last_error_ = std::string("ONNX load error: ") + e.what();
        VP_LOG_ERROR(last_error_);
        return false;
    }
}

std::vector<float> OnnxModel::run(const std::vector<float>& input,
                                   const std::vector<int64_t>& input_shape) {
    if (!loaded_) {
        last_error_ = "Model not loaded";
        return {};
    }

    try {
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info_, const_cast<float*>(input.data()), input.size(),
            input_shape.data(), input_shape.size());

        const char* input_name = input_names_[0].c_str();
        const char* output_name = output_names_[0].c_str();

        auto outputs = session_->Run(Ort::RunOptions{nullptr},
                                     &input_name, &input_tensor, 1,
                                     &output_name, 1);

        // Get output data
        auto& output_tensor = outputs[0];
        auto type_info = output_tensor.GetTensorTypeAndShapeInfo();
        auto output_shape = type_info.GetShape();
        size_t output_size = 1;
        for (auto dim : output_shape) {
            if (dim > 0) output_size *= dim;
        }

        const float* output_data = output_tensor.GetTensorData<float>();
        return std::vector<float>(output_data, output_data + output_size);
    } catch (const Ort::Exception& e) {
        last_error_ = std::string("ONNX inference error: ") + e.what();
        VP_LOG_ERROR(last_error_);
        return {};
    }
}

std::string OnnxModel::get_input_name(int index) const {
    if (index < 0 || index >= static_cast<int>(input_names_.size())) return "";
    return input_names_[index];
}

std::string OnnxModel::get_output_name(int index) const {
    if (index < 0 || index >= static_cast<int>(output_names_.size())) return "";
    return output_names_[index];
}

std::vector<int64_t> OnnxModel::get_input_shape(int index) const {
    if (!loaded_ || index < 0 || index >= static_cast<int>(input_names_.size())) return {};
    auto type_info = session_->GetInputTypeInfo(index);
    auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
    return tensor_info.GetShape();
}

std::vector<int64_t> OnnxModel::get_output_shape(int index) const {
    if (!loaded_ || index < 0 || index >= static_cast<int>(output_names_.size())) return {};
    auto type_info = session_->GetOutputTypeInfo(index);
    auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
    return tensor_info.GetShape();
}

size_t OnnxModel::get_input_count() const {
    return input_names_.size();
}

size_t OnnxModel::get_output_count() const {
    return output_names_.size();
}

} // namespace vp
