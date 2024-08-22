// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <png.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <base/strings/stringprintf.h>
#include <brillo/process/process.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>

#include "lorgnette/libsane_wrapper.h"
#include "lorgnette/libsane_wrapper_impl.h"
#include "lorgnette/sane_client.h"
#include "lorgnette/sane_client_impl.h"

// TODO(b/347749519): Make test report and scans accessible via the guest user.
static const base::FilePath report_root_dir("sane_backend_wwcb_tests_report.d");

namespace sane_backend_tests {
// Declared by GoogleTest main wrapper.
extern const std::string* scanner_under_test;
}  // namespace sane_backend_tests

struct ScanTestParameter {
  const std::string source;
  uint32_t resolution;
  std::string color_mode;
};

// Override ostream so we can get pretty printing of failed parameterized tests.
void operator<<(std::ostream& stream, const ScanTestParameter& param) {
  stream << "source=" << param.source << ", resolution=" << param.resolution
         << ", color_mode=" << param.color_mode;
}

// Returns string that represents where outputs for this specific test goes to.
// Usually this is where scanned images are. This is consistent per test.
static std::string _get_test_output_path() {
  return base::StringPrintf(
      // There current_test_suite()->name() has an embedded "/"
      "%s/%s%s", report_root_dir.value().c_str(),
      testing::UnitTest::GetInstance()->current_test_suite()->name(),
      testing::UnitTest::GetInstance()->current_test_info()->name());
}

class ScanTest : public testing::TestWithParam<ScanTestParameter> {
  void SetUp() override {
    std::filesystem::remove_all(_get_test_output_path());
    std::filesystem::create_directories(_get_test_output_path());
  }
  // Do not remove files on teardown so test artifacts are available for
  // post-test inspection.
  void TearDown() override {}
};

// We need a void function so we can call ASSERT functions.
static void _scan_test_generator(std::vector<ScanTestParameter>& out) {
  std::unique_ptr<lorgnette::LibsaneWrapper> libsane_ =
      lorgnette::LibsaneWrapperImpl::Create();
  std::unique_ptr<lorgnette::SaneClient> sane_client_ =
      lorgnette::SaneClientImpl::Create(libsane_.get());
  brillo::ErrorPtr error;

  std::string ignored;
  std::cout
      << "Press \"enter\" when a single backend supported scanner is attached "
      << "\n";
  std::getline(std::cin, ignored);

  // local=true, we're only testing locally connected scanners
  auto devHandles = sane_client_->ListDevices(&error, true);
  ASSERT_EQ(nullptr, error.get());
  ASSERT_TRUE(devHandles.has_value());

  // Validate the scanner of interest is found by libsane.
  bool scanner_under_test_found = false;
  for (auto dev : devHandles.value()) {
    if (dev.name() == *sane_backend_tests::scanner_under_test) {
      scanner_under_test_found = true;
      break;
    }
  }
  ASSERT_TRUE(scanner_under_test_found)
      << "libsane could not find scanner named "
      << *sane_backend_tests::scanner_under_test;

  SANE_Status status;
  auto sane_dev = sane_client_->ConnectToDevice(
      &error, &status, *sane_backend_tests::scanner_under_test);
  ASSERT_EQ(nullptr, error.get());
  ASSERT_EQ(SANE_STATUS_GOOD, status);

  auto valid_standard_opts = sane_dev->GetValidOptionValues(&error);
  ASSERT_TRUE(valid_standard_opts.has_value());

  auto resolutions = valid_standard_opts.value().resolutions;
  ASSERT_GT(resolutions.size(), 0);
  std::sort(resolutions.begin(), resolutions.end());
  auto min_res = resolutions.front();
  auto max_res = resolutions.back();

  for (auto source : valid_standard_opts.value().sources) {
    for (auto color_mode : valid_standard_opts.value().color_modes) {
      ScanTestParameter min_res_param =
          ScanTestParameter(source.name(), min_res, color_mode);
      out.push_back(min_res_param);

      if (min_res != max_res) {
        ScanTestParameter max_res_param =
            ScanTestParameter(source.name(), max_res, color_mode);
        out.push_back(max_res_param);
      }
    }
  }
}

// copied from lorgnette/cli/file_pattern.cc
static std::string escape_scanner_name(const std::string& scanner_name) {
  std::string escaped;
  for (char c : scanner_name) {
    if (isalnum(c)) {
      escaped += c;
    } else {
      escaped += '_';
    }
  }
  return escaped;
}

static void verify_png_info(const char* filename,
                            uint32_t expected_dpi,
                            const std::string& color_mode) {
  png_structp png_ptr =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  ASSERT_TRUE(png_ptr);

  png_infop info_ptr = png_create_info_struct(png_ptr);
  ASSERT_TRUE(info_ptr);

  FILE* fp = fopen(filename, "rb");
  ASSERT_TRUE(fp);

  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    fclose(fp);
    ASSERT_FALSE("error in libpng");
  }

  png_init_io(png_ptr, fp);
  png_read_info(png_ptr, info_ptr);

  png_uint_32 width, height, res_x, res_y;
  int bit_depth, color_type, res_unit_type;

  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
               nullptr, nullptr, nullptr);
  png_get_pHYs(png_ptr, info_ptr, &res_x, &res_y, &res_unit_type);
  std::cout << "width=" << width << " height=" << height
            << " bit_depth=" << bit_depth << " color_type=" << color_type
            << "\n";
  std::cout << "res_x=" << res_x << " res_y=" << res_y
            << " unit_type=" << res_unit_type << "\n";

  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  fclose(fp);

  // width and height should be within 5% of expected value
  // TODO(b/346842152): support page sizes other than letter
  double expected_width = expected_dpi * 8.5;
  double expected_height = expected_dpi * 11;
  EXPECT_GT(width, expected_width * 0.95);
  EXPECT_LT(width, expected_width * 1.05);
  EXPECT_GT(height, expected_height * 0.95);
  EXPECT_LT(height, expected_height * 1.05);

  // SANE expresses resolution as pixels per inch; PNG pHYs chunk expresses it
  // as pixels per meter. A more accurate conversion would be
  // "round(expected_dpi / .0254)", but multiplying by 39.3701 and truncating to
  // uint32_t is what lorgnette's PngReader does.
  uint32_t expected_dpm = expected_dpi * 39.3701;
  EXPECT_EQ(res_x, res_y);
  EXPECT_EQ(res_x, expected_dpm);
  EXPECT_EQ(res_unit_type, PNG_RESOLUTION_METER);

  // check bit depth/color type
  if (color_mode == "Color") {
    EXPECT_EQ(color_type, PNG_COLOR_TYPE_RGB);
    EXPECT_GT(bit_depth, 1);
  } else if (color_mode == "Gray") {
    EXPECT_EQ(color_type, PNG_COLOR_TYPE_GRAY);
    EXPECT_GT(bit_depth, 1);
  } else if (color_mode == "Lineart") {
    EXPECT_EQ(color_type, PNG_COLOR_TYPE_GRAY);
    EXPECT_EQ(bit_depth, 1);
  }
}

static std::vector<ScanTestParameter> scan_test_generator() {
  std::vector<ScanTestParameter> out;
  // Call a void function so we can use ASSERTs in them.
  _scan_test_generator(out);
  return out;
}

INSTANTIATE_TEST_SUITE_P(
    ScanTestParameters,
    ScanTest,
    testing::ValuesIn(scan_test_generator()),
    [](const testing::TestParamInfo<ScanTest::ParamType>& info) {
      // Can use info.param here to generate the test suffix
      std::string source = info.param.source;
      // GoogleTest test names can only be alphanumeric, so we remove spaces.
      source.erase(
          (std::remove_if(source.begin(), source.end(),
                          [](unsigned char x) { return std::isspace(x); })),
          source.end());
      return base::StringPrintf("SourceIs%sResolutionIs%uColorModeis%s",
                                source.c_str(), info.param.resolution,
                                info.param.color_mode.c_str());
    });

// Runs lorgnette_cli advanced_scan with args; returns exit code.
static int _run_advanced_scan(const std::string& testReportDir,
                              const ScanTestParameter& scanParam,
                              const std::string& scanner) {
  brillo::ProcessImpl lorgnette_cmd;

  lorgnette_cmd.AddArg("/usr/local/bin/lorgnette_cli");
  lorgnette_cmd.AddArg("advanced_scan");
  lorgnette_cmd.AddArg("--scanner=" + scanner);
  lorgnette_cmd.AddArg("set_options");
  lorgnette_cmd.AddArg("resolution=" + std::to_string(scanParam.resolution));
  lorgnette_cmd.AddArg("mode=" + scanParam.color_mode);
  lorgnette_cmd.AddArg("source=" + scanParam.source);

  // %s is the scanner name, %n is the page number
  lorgnette_cmd.AddArg("--output=" + testReportDir + "/%s-page%n.png");

  return lorgnette_cmd.Run();
}

TEST_P(ScanTest, SinglePage) {
  const ScanTestParameter parameter = GetParam();
  std::cout << "Press enter when a page suitable for " << parameter.source
            << " is available for scanning..." << "\n";
  std::string ignored;
  std::getline(std::cin, ignored);

  std::string output_path = _get_test_output_path();

  std::cout << "Scan resolution: " << parameter.resolution << " dpi\n";
  std::cout << "Color mode: " << parameter.color_mode << "\n";
  ASSERT_EQ(_run_advanced_scan(output_path, parameter,
                               *sane_backend_tests::scanner_under_test),
            0);

  std::string image_path =
      output_path + "/" +
      escape_scanner_name(*sane_backend_tests::scanner_under_test) +
      "-page1.png";
  std::cout << "Output path: " << image_path << "\n";
  verify_png_info(image_path.c_str(), parameter.resolution,
                  parameter.color_mode);
}
