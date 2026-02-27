#ifndef VP_ONNX_MODEL_H
#define VP_ONNX_MODEL_H

#include <string>
#include <vector>
#include <memory>
#include <onnxruntime_cxx_api.h>

namespace vp {

class OnnxModel {
public:
    OnnxModel();
    ~OnnxModel();

    // Load model from file
    bool load(const std::string& model_path, Ort::Env& env, int num_threads = 2);

    // Run inference
    std::vector<float> run(const std::vector<float>& input, const std::vector<int64_t>& input_shape);

    // Get input/output info
    std::string get_input_name(int index = 0) const;
    std::string get_output_name(int index = 0) const;
    std::vector<int64_t> get_input_shape(int index = 0) const;
    std::vector<int64_t> get_output_shape(int index = 0) const;
    size_t get_input_count() const;
    size_t get_output_count() const;

    bool is_loaded() const { return loaded_; }
    const std::string& last_error() const { return last_error_; }

private:
    std::unique_ptr<Ort::Session> session_;
    Ort::SessionOptions session_options_;
    Ort::MemoryInfo memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::AllocatorWithDefaultOptions allocator_;

    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;

    bool loaded_ = false;
    std::string last_error_;
};

} // namespace vp

#endif // VP_ONNX_MODEL_H
