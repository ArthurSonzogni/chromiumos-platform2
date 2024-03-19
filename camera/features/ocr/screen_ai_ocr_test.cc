// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <base/strings/string_split.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/callback.h>
#include <base/json/json_file_value_serializer.h>
#include <base/logging.h>
#include <base/scoped_native_library.h>
#include <base/test/bind.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/flag_helper.h>
#include <skia/core/SkBitmap.h>
#include <skia/core/SkData.h>
#include <skia/core/SkImage.h>
#include <gtest/gtest.h>

namespace screen_ai {

namespace {

// The definitions are from
// https://crsrc.org/c/services/screen_ai/screen_ai_library_wrapper_impl.h.
typedef bool (*InitOcrFn)();
typedef char* (*PerformOcrFn)(
    const SkBitmap& /*bitmap*/,
    uint32_t& /*serialized_visual_annotation_length*/);
typedef void (*SetFileContentFunctionsFn)(
    uint32_t (*get_file_content_size)(const char* /*relative_file_path*/),
    void (*get_file_content)(const char* /*relative_file_path*/,
                             uint32_t /*buffer_size*/,
                             char* /*buffer*/));
typedef void (*FreeLibraryAllocatedCharArrayFn)(char* /*memory*/);

constexpr int kWarmUpIterationCount = 3;
constexpr int kActualIterationCount = 5;

constexpr char kLibraryDirectoryPath[] =
    "/run/imageloader/screen-ai/package/root";
constexpr char kLibraryName[] = "libchromescreenai.so";
// The name of the file that contains a list of files that are required to
// initialize the library. The file paths are separated by newlines and
// relative to `kLibraryDirectoryPath`.
constexpr char kFilePathsFileName[] = "files_list_ocr.txt";

SkBitmap GetBitmap(const base::FilePath& path) {
  SkBitmap bitmap;
  sk_sp<SkData> data = SkData::MakeFromFileName(path.value().c_str());
  sk_sp<SkImage> image = SkImage::MakeFromEncoded(data);
  if (!image) {
    LOG(ERROR) << "Failed to create SkImage";
    return bitmap;
  }
  image->asLegacyBitmap(&bitmap);
  return bitmap;
}

}  // namespace

class OcrTestEnvironment;
OcrTestEnvironment* g_env;

class OcrTestEnvironment : public ::testing::Environment {
 public:
  static inline std::map<std::string, std::vector<uint8_t>> data_;

  static uint32_t GetDataSize(const char* relative_file_path) {
    return OcrTestEnvironment::data_[relative_file_path].size();
  }

  static void CopyData(const char* relative_file_path,
                       uint32_t buffer_size,
                       char* buffer) {
    const std::vector<uint8_t>& data =
        OcrTestEnvironment::data_[relative_file_path];
    CHECK_GE(buffer_size, data.size());
    memcpy(buffer, data.data(), data.size());
  }

  OcrTestEnvironment(const std::string& output_path,
                     const std::string& jpeg_image_path)
      : output_path_(output_path),
        jpeg_image_(GetBitmap(base::FilePath(jpeg_image_path))) {}

  void SetUp() override {
    ASSERT_FALSE(jpeg_image_.empty());

    base::FilePath directory_path(kLibraryDirectoryPath);
    base::FilePath library_path = directory_path.Append(kLibraryName);
    library_ = base::ScopedNativeLibrary(library_path);
    ASSERT_TRUE(library_.is_valid())
        << "Library is invalid. "
        << "Run `dlcservice_util --id=screen-ai --install` to install the lib.";

    ASSERT_TRUE(
        LoadFunction(set_file_content_functions_, "SetFileContentFunctions") &&
        LoadFunction(init_ocr_, "InitOCRUsingCallback") &&
        LoadFunction(perform_ocr_, "PerformOCR") &&
        LoadFunction(free_library_allocated_char_array_,
                     "FreeLibraryAllocatedCharArray"));

    set_file_content_functions_(&OcrTestEnvironment::GetDataSize,
                                &OcrTestEnvironment::CopyData);
    ASSERT_TRUE(PrepareModelData());
    init_ocr_();
  }

  template <typename T>
  bool LoadFunction(T& function_variable, const char* function_name) {
    function_variable =
        reinterpret_cast<T>(library_.GetFunctionPointer(function_name));
    if (function_variable == nullptr) {
      LOG(ERROR) << "Could not load function: " << function_name;
      return false;
    }
    return true;
  }

  bool PrepareModelData() {
    base::FilePath directory_path(kLibraryDirectoryPath);
    base::FilePath file_paths_path = directory_path.Append(kFilePathsFileName);
    std::string file_content;
    if (!base::ReadFileToString(file_paths_path, &file_content)) {
      LOG(ERROR) << "Could not read list of files for " << kFilePathsFileName;
      return false;
    }
    std::vector<std::string> files_list = base::SplitString(
        file_content, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    if (files_list.empty()) {
      LOG(ERROR) << "Could not parse files list for " << kFilePathsFileName;
      return false;
    }
    for (auto& relative_file_path : files_list) {
      // Ignore comment lines.
      if (relative_file_path.empty() || relative_file_path[0] == '#') {
        continue;
      }
      LOG(INFO) << "Load model file: " << relative_file_path;
      std::optional<std::vector<uint8_t>> buffer =
          base::ReadFileToBytes(directory_path.Append(relative_file_path));
      if (!buffer) {
        LOG(ERROR) << "Could not read file's content: " << relative_file_path;
        return false;
      }
      OcrTestEnvironment::data_[relative_file_path] = std::move(*buffer);
    }
    return true;
  }

  void PerformOcr() {
    uint32_t annotation_proto_length = 0;
    // TODO(b/326872468): Print extracted text information.
    std::unique_ptr<char> library_buffer(
        perform_ocr_(jpeg_image_, annotation_proto_length));
    free_library_allocated_char_array_(library_buffer.release());
  }

  void Benchmark(const std::string& metrics_name,
                 base::RepeatingClosure target_ops) {
    for (int i = 0; i < kWarmUpIterationCount; ++i) {
      target_ops.Run();
    }

    base::ElapsedTimer timer;
    for (int i = 0; i < kActualIterationCount; ++i) {
      target_ops.Run();
    }
    int avg_duration = timer.Elapsed().InMilliseconds() / kActualIterationCount;
    perf_values_.Set(metrics_name, avg_duration);

    LOG(INFO) << "Perf: " << metrics_name << " => " << avg_duration << " ms";
  }

  void TearDown() override {
    JSONFileValueSerializer json_serializer(output_path_);
    EXPECT_TRUE(json_serializer.Serialize(perf_values_));
  }

  base::Value::Dict perf_values_;
  base::FilePath output_path_;
  SkBitmap jpeg_image_;
  base::ScopedNativeLibrary library_;
  InitOcrFn init_ocr_;
  PerformOcrFn perform_ocr_;
  SetFileContentFunctionsFn set_file_content_functions_;
  FreeLibraryAllocatedCharArrayFn free_library_allocated_char_array_;
};

class OcrPerfTest : public ::testing::Test {
 protected:
  OcrPerfTest() = default;
};

TEST_F(OcrPerfTest, PerformOcr) {
  g_env->Benchmark("PerformOcr",
                   base::BindLambdaForTesting([&]() { g_env->PerformOcr(); }));
}

}  // namespace screen_ai

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  DEFINE_string(output_path, "", "The path to store the output perf result");
  DEFINE_string(jpeg_image, "", "The test image in JPEG format");

  // Add a newline at the beginning of the usage text to separate the help
  // message from gtest.
  brillo::FlagHelper::Init(argc, argv, "\nTest Screen AI OCR.");

  if (FLAGS_output_path.empty()) {
    LOG(ERROR) << "No output path is specified";
    return -1;
  }
  if (FLAGS_jpeg_image.empty()) {
    LOG(ERROR) << "No jpeg image is specified";
    return -1;
  }

  screen_ai::g_env =
      new screen_ai::OcrTestEnvironment(FLAGS_output_path, FLAGS_jpeg_image);
  ::testing::AddGlobalTestEnvironment(screen_ai::g_env);
  return RUN_ALL_TESTS();
}
