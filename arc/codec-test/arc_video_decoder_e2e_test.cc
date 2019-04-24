// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>

#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "arc/codec-test/common.h"
#include "arc/codec-test/mediacodec_decoder.h"
#include "arc/codec-test/video_frame.h"

namespace android {

// Environment to store test video data for all test cases.
class ArcVideoDecoderTestEnvironment;

namespace {
ArcVideoDecoderTestEnvironment* g_env;
}  // namespace

class ArcVideoDecoderTestEnvironment : public testing::Environment {
 public:
  explicit ArcVideoDecoderTestEnvironment(const std::string& data,
                                          const std::string& output_frames_path)
      : test_video_data_(data), output_frames_path_(output_frames_path) {}

  void SetUp() override { ParseTestVideoData(); }

  // The syntax of test video data is:
  // "input_file_path:width:height:num_frames:num_fragments:min_fps_render:
  //  min_fps_no_render:video_codec_profile[:output_file_path]"
  // - |input_file_path| is compressed video stream in H264 Annex B (NAL) format
  //   (H264) or IVF (VP8/9).
  // - |width| and |height| are visible frame size in pixels.
  // - |num_frames| is the number of picture frames for the input stream.
  // - |num_fragments| is the number of AU (H264) or frame (VP8/9) in the input
  //   stream. (Unused. Test will automatically parse the number.)
  // - |min_fps_render| and |min_fps_no_render| are minimum frames/second speeds
  //   expected to be achieved with and without rendering respective.
  //   (The former is unused because no rendering case here.)
  //   (The latter is Optional.)
  // - |video_codec_profile| is the VideoCodecProfile set during Initialization.
  void ParseTestVideoData() {
    std::vector<std::string> fields = SplitString(test_video_data_, ':');
    ASSERT_EQ(fields.size(), 8U)
        << "The number of fields of test_video_data is not 8: "
        << test_video_data_;

    input_file_path_ = fields[0];
    int width = std::stoi(fields[1]);
    int height = std::stoi(fields[2]);
    visible_size_ = Size(width, height);
    ASSERT_FALSE(visible_size_.IsEmpty());

    num_frames_ = std::stoi(fields[3]);
    ASSERT_GT(num_frames_, 0);

    // Unused fields[4] --> num_fragments
    // Unused fields[5] --> min_fps_render

    if (!fields[6].empty()) {
      min_fps_no_render_ = std::stoi(fields[6]);
    }

    video_codec_profile_ = static_cast<VideoCodecProfile>(std::stoi(fields[7]));
    ASSERT_NE(VideoCodecProfileToType(video_codec_profile_),
              VideoCodecType::UNKNOWN);
  }

  // Get the corresponding frame-wise golden MD5 file path.
  std::string GoldenMD5FilePath() const {
    return input_file_path_ + ".frames.md5";
  }

  std::string output_frames_path() const { return output_frames_path_; }

  std::string input_file_path() const { return input_file_path_; }
  Size visible_size() const { return visible_size_; }
  int num_frames() const { return num_frames_; }
  int min_fps_no_render() const { return min_fps_no_render_; }
  VideoCodecProfile video_codec_profile() const { return video_codec_profile_; }

 protected:
  std::string test_video_data_;
  std::string output_frames_path_;

  std::string input_file_path_;
  Size visible_size_;
  int num_frames_ = 0;
  int min_fps_no_render_ = 0;
  VideoCodecProfile video_codec_profile_;
};

// The struct to record output formats.
struct OutputFormat {
  Size coded_size;
  Size visible_size;
  int32_t color_format = 0;
};

// The helper class to validate video frame by MD5 and ouput to I420 raw stream
// if needed.
class VideoFrameValidator {
 public:
  VideoFrameValidator() = default;
  ~VideoFrameValidator() { output_file_.close(); }

  // Set |md5_golden_path| as the path of golden frame-wise MD5 file. Return
  // false if the file is failed to read.
  bool SetGoldenMD5File(const std::string& md5_golden_path) {
    golden_md5_file_ =
        std::unique_ptr<InputFileASCII>(new InputFileASCII(md5_golden_path));
    return golden_md5_file_->IsValid();
  }

  // Set |output_frames_path| as the path for output raw I420 stream. Return
  // false if the file is failed to open.
  bool SetOutputFile(const std::string& output_frames_path) {
    if (output_frames_path.empty())
      return false;

    output_file_.open(output_frames_path, std::ofstream::binary);
    if (!output_file_.is_open()) {
      printf("[ERR] Failed to open file: %s\n", output_frames_path.c_str());
      return false;
    }
    printf("[LOG] Decode output to file: %s\n", output_frames_path.c_str());
    write_to_file_ = true;
    return true;
  }

  // Callback function of output buffer ready to validate frame data by
  // VideoFrameValidator, write into file if needed.
  void VerifyMD5(const uint8_t* data, size_t buffer_size, int output_index) {
    std::string golden;
    ASSERT_TRUE(golden_md5_file_ && golden_md5_file_->IsValid());
    ASSERT_TRUE(golden_md5_file_->ReadLine(&golden))
        << "Failed to read golden MD5 at frame#" << output_index;

    std::unique_ptr<VideoFrame> video_frame = VideoFrame::Create(
        data, buffer_size, output_format_.coded_size,
        output_format_.visible_size, output_format_.color_format);
    ASSERT_TRUE(video_frame)
        << "Failed to create video frame on VerifyMD5 at frame#"
        << output_index;

    ASSERT_TRUE(video_frame->VerifyMD5(golden))
        << "MD5 mismatched at frame#" << output_index;

    // Update color_format.
    output_format_.color_format = video_frame->color_format();
  }

  // Callback function of output buffer ready to validate frame data by
  // VideoFrameValidator, write into file if needed.
  void OutputToFile(const uint8_t* data, size_t buffer_size, int output_index) {
    if (!write_to_file_)
      return;

    std::unique_ptr<VideoFrame> video_frame = VideoFrame::Create(
        data, buffer_size, output_format_.coded_size,
        output_format_.visible_size, output_format_.color_format);
    ASSERT_TRUE(video_frame)
        << "Failed to create video frame on OutputToFile at frame#"
        << output_index;
    if (!video_frame->WriteFrame(&output_file_)) {
      printf("[ERR] Failed to write output buffer into file.\n");
      // Stop writing frames to file once it is failed.
      write_to_file_ = false;
    }
  }

  // Callback function of output format changed to update output format.
  void UpdateOutputFormat(const Size& coded_size,
                          const Size& visible_size,
                          int32_t color_format) {
    output_format_.coded_size = coded_size;
    output_format_.visible_size = visible_size;
    output_format_.color_format = color_format;
  }

 private:
  // The wrapper of input MD5 golden file.
  std::unique_ptr<InputFileASCII> golden_md5_file_;
  // The output file to write the decoded raw video.
  std::ofstream output_file_;

  // Only output video frame to file if True.
  bool write_to_file_ = false;
  // This records output format, color_format might be revised in flexible
  // format case.
  OutputFormat output_format_;
};

class ArcVideoDecoderE2ETest : public testing::Test {
 public:
  //  Callback function of output buffer ready to count frame.
  void CountFrame(const uint8_t* /* data */,
                  size_t /* buffer_size */,
                  int /* output_index */) {
    decoded_frames_++;
  }

  // Callback function of output format changed to verify output format.
  void VerifyOutputFormat(const Size& coded_size,
                          const Size& visible_size,
                          int32_t color_format) {
    ASSERT_FALSE(coded_size.IsEmpty());
    ASSERT_FALSE(visible_size.IsEmpty());
    ASSERT_LE(visible_size.width, coded_size.width);
    ASSERT_LE(visible_size.height, coded_size.height);
    printf(
        "[LOG] Got format changed { coded_size: %dx%d, visible_size: %dx%d, "
        "color_format: 0x%x\n",
        coded_size.width, coded_size.height, visible_size.width,
        visible_size.height, color_format);
    output_format_.coded_size = coded_size;
    output_format_.visible_size = visible_size;
    output_format_.color_format = color_format;
  }

 protected:
  void SetUp() override {
    decoder_ = MediaCodecDecoder::Create(g_env->input_file_path(),
                                         g_env->video_codec_profile(),
                                         g_env->visible_size());

    ASSERT_TRUE(decoder_);
    decoder_->Rewind();

    ASSERT_TRUE(decoder_->Configure());
    ASSERT_TRUE(decoder_->Start());

    decoder_->AddOutputBufferReadyCb(std::bind(
        &ArcVideoDecoderE2ETest::CountFrame, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3));
  }

  void TearDown() override {
    EXPECT_TRUE(decoder_->Stop());

    EXPECT_EQ(g_env->visible_size().width, output_format_.visible_size.width);
    EXPECT_EQ(g_env->visible_size().height, output_format_.visible_size.height);
    EXPECT_EQ(g_env->num_frames(), decoded_frames_);

    decoder_.reset();
  }

  // The wrapper of the mediacodec decoder.
  std::unique_ptr<MediaCodecDecoder> decoder_;

  // The counter of obtained decoded output frames.
  int decoded_frames_ = 0;
  // This records formats from output format change.
  OutputFormat output_format_;
};

TEST_F(ArcVideoDecoderE2ETest, TestSimpleDecode) {
  VideoFrameValidator video_frame_validator;

  ASSERT_TRUE(
      video_frame_validator.SetGoldenMD5File(g_env->GoldenMD5FilePath()))
      << "Failed to open MD5 file: " << g_env->GoldenMD5FilePath();

  decoder_->AddOutputBufferReadyCb(std::bind(
      &VideoFrameValidator::VerifyMD5, &video_frame_validator,
      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

  if (video_frame_validator.SetOutputFile(g_env->output_frames_path())) {
    decoder_->AddOutputBufferReadyCb(std::bind(
        &VideoFrameValidator::OutputToFile, &video_frame_validator,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
  }

  decoder_->AddOutputFormatChangedCb(std::bind(
      &ArcVideoDecoderE2ETest::VerifyOutputFormat, this, std::placeholders::_1,
      std::placeholders::_2, std::placeholders::_3));
  decoder_->AddOutputFormatChangedCb(std::bind(
      &VideoFrameValidator::UpdateOutputFormat, &video_frame_validator,
      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

  EXPECT_TRUE(decoder_->Decode());
}

TEST_F(ArcVideoDecoderE2ETest, TestFPS) {
  decoder_->AddOutputFormatChangedCb(std::bind(
      &ArcVideoDecoderE2ETest::VerifyOutputFormat, this, std::placeholders::_1,
      std::placeholders::_2, std::placeholders::_3));

  int64_t time_before_decode_us = GetNowUs();
  EXPECT_TRUE(decoder_->Decode());
  int64_t total_decode_time_us = GetNowUs() - time_before_decode_us;

  double fps = decoded_frames_ * 1E6 / total_decode_time_us;
  printf("[LOG] Measured decoder FPS: %.4f\n", fps);
  // TODO(johnylin): improve FPS calculation by CTS method and then enable the
  //                 following check.
  // EXPECT_GE(fps, static_cast<double>(g_env->min_fps_no_render()));
}

}  // namespace android

bool GetOption(int argc,
               char** argv,
               std::string* test_video_data,
               std::string* output_frames_path) {
  const char* const optstring = "t:o:";
  static const struct option opts[] = {
      {"test_video_data", required_argument, nullptr, 't'},
      {"output_frames_path", required_argument, nullptr, 'o'},
      {nullptr, 0, nullptr, 0},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, optstring, opts, nullptr)) != -1) {
    switch (opt) {
      case 't':
        *test_video_data = optarg;
        break;
      case 'o':
        *output_frames_path = optarg;
        break;
      default:
        printf("[WARN] Unknown option: getopt_long() returned code 0x%x.\n",
               opt);
        break;
    }
  }

  if (test_video_data->empty()) {
    printf("[ERR] Please assign test video data by --test_video_data\n");
    return false;
  }
  return true;
}

int main(int argc, char** argv) {
  std::string test_video_data;
  std::string output_frames_path;
  if (!GetOption(argc, argv, &test_video_data, &output_frames_path))
    return EXIT_FAILURE;

  android::g_env = reinterpret_cast<android::ArcVideoDecoderTestEnvironment*>(
      testing::AddGlobalTestEnvironment(
          new android::ArcVideoDecoderTestEnvironment(test_video_data,
                                                      output_frames_path)));
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
