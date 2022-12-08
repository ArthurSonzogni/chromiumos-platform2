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
#include <chromeos/dbus/debugd/dbus-constants.h>

#include "debugd/src/cups_tool.h"

namespace debugd {

namespace {

constexpr base::StringPiece kMinimalPPDContent(R"PPD(*PPD-Adobe: "4.3"
*FormatVersion: "4.3"
*FileVersion: "1.0"
*LanguageVersion: English
*LanguageEncoding: ISOLatin1
*PCFileName: "SAMPLE.PPD"
*Product: "(Sample)"
*PSVersion: "(1) 1"
*ModelName: "Sample"
*ShortNickName: "Sample"
*NickName: "Sample"
*Manufacturer: "Sample"
*OpenUI *PageSize: PickOne
*DefaultPageSize: A4
*PageSize A4/A4: "<</PageSize[595.20 841.68]>>setpagedevice"
*CloseUI: *PageSize
*OpenUI *PageRegion: PickOne
*DefaultPageRegion: A4
*PageRegion A4/A4: "<</PageRegion[595.20 841.68]>>setpagedevice"
*CloseUI: *PageRegion
*DefaultImageableArea: A4
*ImageableArea A4/A4: "8.40 8.40 586.80 833.28"
*DefaultPaperDimension: A4
*PaperDimension A4/A4: "595.20 841.68"
)PPD");

}  // namespace

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

  int CupsTestPpd(const std::vector<uint8_t>&) const override {
    return cupstestppd_result_;
  }

  int CupsUriHelper(const std::string& uri) const override {
    return urihelper_result_;
  }

  const base::FilePath& GetCupsPpdDir() const override {
    return ppd_dir_.GetPath();
  }

  // The following methods allow the user to setup the fake object to return the
  // desired results.

  void SetLpstatOutput(const std::string& data) { lpstat_output_ = data; }

  void SetCupsTestPPDResult(int result) { cupstestppd_result_ = result; }

  void SetCupsUriHelperResult(int result) { urihelper_result_ = result; }

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
  int cupstestppd_result_{0};
  int urihelper_result_{0};
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

TEST(CupsToolTest, InvalidPPDTooSmall) {
  std::vector<uint8_t> empty_ppd;

  CupsTool cups;
  EXPECT_EQ(cups.AddManuallyConfiguredPrinter("test", "ipp://", empty_ppd),
            CupsResult::CUPS_INVALID_PPD);
}

TEST(CupsToolTest, InvalidPPDBadGzip) {
  // Make the PPD look like it's gzipped.
  std::vector<uint8_t> bad_ppd(kMinimalPPDContent.begin(),
                               kMinimalPPDContent.end());
  bad_ppd[0] = 0x1f;
  bad_ppd[1] = 0x8b;

  CupsTool cups;
  EXPECT_EQ(cups.AddManuallyConfiguredPrinter("test", "ipp://", bad_ppd),
            CupsResult::CUPS_INVALID_PPD);
}

TEST(CupsToolTest, InvalidPPDBadContents) {
  // Corrupt a valid PPD so it won't validate.
  std::vector<uint8_t> bad_ppd(kMinimalPPDContent.begin(),
                               kMinimalPPDContent.end());
  bad_ppd[0] = 'X';
  bad_ppd[1] = 'Y';
  bad_ppd[2] = 'Z';

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsTestPPDResult(4);  // Typical failure exit code.

  CupsTool cups;
  cups.SetLpToolsForTesting(std::move(lptools));

  EXPECT_EQ(cups.AddManuallyConfiguredPrinter("test", "ipp://", bad_ppd),
            CupsResult::CUPS_INVALID_PPD);
}

TEST(CupsToolTest, ManualMissingURI) {
  std::vector<uint8_t> good_ppd(kMinimalPPDContent.begin(),
                                kMinimalPPDContent.end());

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsTestPPDResult(0);  // Successful validation.

  CupsTool cups;
  cups.SetLpToolsForTesting(std::move(lptools));

  EXPECT_EQ(cups.AddManuallyConfiguredPrinter("test", "", good_ppd),
            CupsResult::CUPS_BAD_URI);
}

TEST(CupsToolTest, ManualMissingName) {
  std::vector<uint8_t> good_ppd(kMinimalPPDContent.begin(),
                                kMinimalPPDContent.end());

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsTestPPDResult(0);    // Successful validation.
  lptools->SetCupsUriHelperResult(0);  // URI validated.

  CupsTool cups;
  cups.SetLpToolsForTesting(std::move(lptools));

  EXPECT_EQ(cups.AddManuallyConfiguredPrinter(
                "", "ipp://127.0.0.1:631/ipp/print", good_ppd),
            CupsResult::CUPS_FATAL);
}

TEST(CupsToolTest, AutoMissingURI) {
  CupsTool cups;
  EXPECT_EQ(cups.AddAutoConfiguredPrinter("test", ""), CupsResult::CUPS_FATAL);
}

TEST(CupsToolTest, AutoMissingName) {
  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(0);  // URI validated.

  CupsTool cups;
  cups.SetLpToolsForTesting(std::move(lptools));

  EXPECT_EQ(cups.AddAutoConfiguredPrinter("", "ipp://127.0.0.1:631/ipp/print"),
            CupsResult::CUPS_FATAL);
}

}  // namespace debugd
