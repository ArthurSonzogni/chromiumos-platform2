// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/process/process.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>
#include <string>
#include <vector>

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
};

// Override ostream so we can get pretty printing of failed parameterized tests.
void operator<<(std::ostream& stream, const ScanTestParameter& param) {
  stream << "source=" << param.source;
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

// Returns true for "y" and false for "n"
static bool _y_or_no() {
  do {
    std::string answer;
    std::getline(std::cin, answer);
    answer = base::ToLowerASCII(answer);
    if (answer == "y") {
      return true;
    } else if (answer == "n") {
      return false;
    } else {
      std::cout << "Please answer \"y\" or \"n\"" << "\n";
    }
  } while (true);
}

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
  for (auto opt : valid_standard_opts.value().sources) {
    ScanTestParameter new_param = ScanTestParameter(opt.name());
    out.push_back(new_param);
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
      return "SourceIs" + source;
    });

// Runs lorgnette_cli advanced_scan with args; returns exit code.
static int _run_advanced_scan(const std::string& testReportDir,
                              const std::string& scanSource,
                              const std::string& scanner) {
  brillo::ProcessImpl lorgnette_cmd;

  lorgnette_cmd.AddArg("/usr/local/bin/lorgnette_cli");
  lorgnette_cmd.AddArg("advanced_scan");
  lorgnette_cmd.AddArg("--scanner=" + scanner);
  lorgnette_cmd.AddArg("set_options");
  lorgnette_cmd.AddArg("source=" + scanSource);

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

  ASSERT_EQ(_run_advanced_scan(output_path, parameter.source,
                               *sane_backend_tests::scanner_under_test),
            0);

  // TODO(b/346608170) Validate with "identify -verbose" command

  std::cout << "Do scans under " << output_path << " look OK (y/n):\n";
  ASSERT_TRUE(_y_or_no());
}
