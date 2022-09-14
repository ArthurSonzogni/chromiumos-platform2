// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <base/files/scoped_temp_dir.h>
#include <base/files/file_util.h>
#include <base/files/file_path.h>
#include <base/strings/stringprintf.h>

#include "debugd/src/cups_tool.h"

namespace debugd {

class FakeLpTools : public LpTools {
 public:
  FakeLpTools() { CHECK(ppd_dir_.CreateUniqueTempDir()); }

  int Lpadmin(const ProcessWithOutput::ArgList& arg_list,
              bool inherit_usergroups,
              const std::vector<uint8_t>* std_input) override {
    return 0;
  }

  // Return 1 if lpstat_output_ is empty, else populate output (if non-null) and
  // return 0.
  int Lpstat(const ProcessWithOutput::ArgList& arg_list,
             std::string* output) override {
    if (lpstat_output_.empty()) {
      return 1;
    }

    if (output != nullptr) {
      *output = lpstat_output_;
    }

    return 0;
  }

  const base::FilePath& GetCupsPpdDir() const override {
    return ppd_dir_.GetPath();
  }

  // The following methods allow the user to setup the fake object to return the
  // desired results.

  void SetLpstatOutput(const std::string& data) { lpstat_output_ = data; }

  // Create some valid output for lpstat based on printerName
  void CreateValidLpstatOutput(const std::string& printerName) {
    const std::string lpstatOutput = base::StringPrintf(
        R"(printer %s is idle.
  Form mounted:
  Content types: any
  Printer types: unknown
  Description: %s
  Alerts: none
  Connection: direct
  Interface: %s/%s.ppd
  On fault: no alert
  After fault: continue
  Users allowed:
    (all)
  Forms allowed:
    (none)
  Banner required
  Charset sets:
    (none)
  Default pitch:
  Default page size:
  Default port settings:
  )",
        printerName.c_str(), printerName.c_str(),
        ppd_dir_.GetPath().value().c_str(), printerName.c_str());

    SetLpstatOutput(lpstatOutput);
  }

 private:
  std::string lpstat_output_;
  base::ScopedTempDir ppd_dir_;
};

TEST(CupsToolTest, RetrievePpd) {
  // Test the case where everything works as expected.

  // Create a fake lp tools object and configure it so we know what results we
  // should expect from CupsTool.
  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();

  const std::string printerName("test-printer");
  lptools->CreateValidLpstatOutput(printerName);
  const base::FilePath& ppdDir = lptools->GetCupsPpdDir();
  const base::FilePath ppdPath = ppdDir.Append(printerName + ".ppd");

  // Create our ppd file that will get read by CupsTool
  const std::vector<uint8_t> ppdContents = {'T', 'e', 's', 't', ' ', 'd', 'a',
                                            't', 'a', ' ', 'i', 'n', ' ', 'P',
                                            'P', 'D', ' ', 'f', 'i', 'l', 'e'};
  ASSERT_TRUE(base::WriteFile(ppdPath, ppdContents));

  CupsTool cupsTool;
  cupsTool.SetLpToolsForTesting(std::move(lptools));

  std::vector<uint8_t> retrievedData = cupsTool.RetrievePpd(printerName);

  EXPECT_THAT(ppdContents, testing::ContainerEq(retrievedData));
}

TEST(CupsToolTest, EmptyFile) {
  // Test the case where the PPD file is empty.

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();

  const std::string printerName("test-printer");
  lptools->CreateValidLpstatOutput(printerName);
  const base::FilePath& ppdDir = lptools->GetCupsPpdDir();
  const base::FilePath ppdPath = ppdDir.Append(printerName + ".ppd");

  // Create an empty ppd file that will get read by CupsTool
  const std::string ppdContents("");
  ASSERT_TRUE(base::WriteFile(ppdPath, ppdContents));

  CupsTool cupsTool;
  cupsTool.SetLpToolsForTesting(std::move(lptools));
  const std::vector<uint8_t> retrievedData = cupsTool.RetrievePpd(printerName);

  EXPECT_TRUE(retrievedData.empty());
}

TEST(CupsToolTest, PpdFileDoesNotExist) {
  // Test the case where lpstat works as expected, but the PPD file does not
  // exist.

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();

  const std::string printerName("test-printer");
  lptools->CreateValidLpstatOutput(printerName);

  CupsTool cupsTool;
  cupsTool.SetLpToolsForTesting(std::move(lptools));

  const std::vector<uint8_t> retrievedPpd = cupsTool.RetrievePpd(printerName);

  EXPECT_TRUE(retrievedPpd.empty());
}

TEST(CupsToolTest, LpstatError) {
  // Test the case where there is an error running lpstat

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();

  // Since we have not specified the lpstat output, our fake object will return
  // an error from running lpstat.

  CupsTool cupsTool;
  cupsTool.SetLpToolsForTesting(std::move(lptools));

  const std::vector<uint8_t> retrievedPpd = cupsTool.RetrievePpd("printer");

  EXPECT_TRUE(retrievedPpd.empty());
}

TEST(CupsToolTest, LpstatNoPrinter) {
  // Test the case where lpstat runs but doesn't return the printer we are
  // looking for.

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();

  const std::string printerName("test-printer");
  lptools->SetLpstatOutput("lpstat data not containing our printer name");

  CupsTool cupsTool;
  cupsTool.SetLpToolsForTesting(std::move(lptools));

  const std::vector<uint8_t> retrievedPpd = cupsTool.RetrievePpd(printerName);

  EXPECT_TRUE(retrievedPpd.empty());
}

}  // namespace debugd
