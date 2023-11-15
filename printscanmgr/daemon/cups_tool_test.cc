// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <base/files/scoped_temp_dir.h>
#include <base/files/file_util.h>
#include <base/files/file_path.h>
#include <base/strings/stringprintf.h>
#include <base/test/task_environment.h>
#include <chromeos/dbus/printscanmgr/dbus-constants.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <printscanmgr/proto_bindings/printscanmgr_service.pb.h>

#include "printscanmgr/daemon/cups_tool.h"
#include "printscanmgr/executor/mock_executor.h"
#include "printscanmgr/mojom/executor.mojom.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::WithArg;

namespace printscanmgr {

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

// Constructs a CupsAddAutoConfiguredPrinterRequest from `name` and `uri`.
CupsAddAutoConfiguredPrinterRequest
ConstructCupsAddAutoConfiguredPrinterRequest(const std::string& name,
                                             const std::string& uri) {
  CupsAddAutoConfiguredPrinterRequest request;
  request.set_name(name);
  request.set_uri(uri);
  return request;
}

// Constructs a CupsAddManuallyConfiguredPrinterRequest from `name`, `uri` and
// `ppd_contents`.
CupsAddManuallyConfiguredPrinterRequest
ConstructCupsAddManuallyConfiguredPrinterRequest(
    const std::string& name,
    const std::string& uri,
    const std::vector<uint8_t>& ppd_contents) {
  CupsAddManuallyConfiguredPrinterRequest request;
  request.set_name(name);
  request.set_uri(uri);
  request.set_ppd_contents(
      std::string(ppd_contents.begin(), ppd_contents.end()));
  return request;
}

// Constructs a CupsRemovePrinterRequest from `name`.
CupsRemovePrinterRequest ConstructCupsRemovePrinterRequest(
    const std::string& name) {
  CupsRemovePrinterRequest request;
  request.set_name(name);
  return request;
}

// Constructs a CupsRetrievePpdRequest from `name`.
CupsRetrievePpdRequest ConstructCupsRetrievePpdRequest(
    const std::string& name) {
  CupsRetrievePpdRequest request;
  request.set_name(name);
  return request;
}

}  // namespace

class FakeLpTools : public LpTools {
 public:
  FakeLpTools() { CHECK(ppd_dir_.CreateUniqueTempDir()); }

  int Lpadmin(const std::vector<std::string>& arg_list,
              const std::vector<uint8_t>* std_input) override {
    return lpadmin_result_;
  }

  // Return 1 if lpstat_output_ is empty, else populate output (if non-null) and
  // return 0.
  int Lpstat(const std::vector<std::string>& arg_list,
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

  bool CupsUriHelper(const std::string& uri) const override {
    return urihelper_result_;
  }

  int RunCommand(const std::string& command,
                 const std::vector<std::string>& arg_list,
                 const std::vector<uint8_t>* std_input = nullptr,
                 std::string* out = nullptr) const override {
    return runcommand_result_;
  }

  // The following methods allow the user to setup the fake object to return the
  // desired results.

  void SetLpstatOutput(const std::string& data) { lpstat_output_ = data; }

  void SetCupsTestPPDResult(int result) { cupstestppd_result_ = result; }

  void SetCupsUriHelperResult(bool result) { urihelper_result_ = result; }

  void SetRunCommandResult(int result) { runcommand_result_ = result; }

  void SetLpadminResult(int result) { lpadmin_result_ = result; }

  // Create some valid output for lpstat based on `printerName`.
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
  bool urihelper_result_{true};
  int runcommand_result_{0};
  int lpadmin_result_{0};
};

TEST(CupsToolTest, RetrievePpd) {
  // Test the case where everything works as expected.
  base::test::SingleThreadTaskEnvironment task_environment;

  // Create a fake lp tools object and configure it so we know what results we
  // should expect from CupsTool.
  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();

  const std::string printerName("test-printer");
  lptools->CreateValidLpstatOutput(printerName);

  // Create our ppd string that will get returned by the executor.
  const std::string ppdContents = "Test data in PPD file";

  StrictMock<MockExecutor> mock_executor;
  EXPECT_CALL(mock_executor, GetPpdFile(printerName + ".ppd", _))
      .WillOnce(
          WithArg<1>(Invoke([&](mojom::Executor::GetPpdFileCallback callback) {
            std::move(callback).Run(/*file_contents=*/ppdContents,
                                    /*success=*/true);
          })));

  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cupsTool(remote.get());
  cupsTool.SetLpToolsForTesting(std::move(lptools));

  CupsRetrievePpdResponse response =
      cupsTool.RetrievePpd(ConstructCupsRetrievePpdRequest(printerName));

  EXPECT_EQ(ppdContents, response.ppd());
}

TEST(CupsToolTest, EmptyStringFromExecutor) {
  // Test the case where the executor returns an empty string.
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();

  const std::string printerName("test-printer");
  lptools->CreateValidLpstatOutput(printerName);

  // Create an empty ppd string that will get returned by the executor.
  const std::string ppdContents("");

  StrictMock<MockExecutor> mock_executor;
  EXPECT_CALL(mock_executor, GetPpdFile(printerName + ".ppd", _))
      .WillOnce(
          WithArg<1>(Invoke([&](mojom::Executor::GetPpdFileCallback callback) {
            std::move(callback).Run(/*file_contents=*/ppdContents,
                                    /*success=*/true);
          })));

  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cupsTool(remote.get());
  cupsTool.SetLpToolsForTesting(std::move(lptools));
  CupsRetrievePpdResponse response =
      cupsTool.RetrievePpd(ConstructCupsRetrievePpdRequest(printerName));

  EXPECT_TRUE(response.ppd().empty());
}

TEST(CupsToolTest, ExecutorCallFails) {
  // Test the case where lpstat works as expected, but the PPD file does not
  // exist.
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();

  const std::string printerName("test-printer");
  lptools->CreateValidLpstatOutput(printerName);

  StrictMock<MockExecutor> mock_executor;
  EXPECT_CALL(mock_executor, GetPpdFile(printerName + ".ppd", _))
      .WillOnce(
          WithArg<1>(Invoke([&](mojom::Executor::GetPpdFileCallback callback) {
            std::move(callback).Run(/*file_contents=*/"",
                                    /*success=*/false);
          })));

  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cupsTool(remote.get());
  cupsTool.SetLpToolsForTesting(std::move(lptools));

  CupsRetrievePpdResponse response =
      cupsTool.RetrievePpd(ConstructCupsRetrievePpdRequest(printerName));

  EXPECT_TRUE(response.ppd().empty());
}

TEST(CupsToolTest, LpstatError) {
  // Test the case where there is an error running lpstat
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();

  // Since we have not specified the lpstat output, our fake object will return
  // an error from running lpstat.

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cupsTool(remote.get());
  cupsTool.SetLpToolsForTesting(std::move(lptools));

  CupsRetrievePpdResponse response =
      cupsTool.RetrievePpd(ConstructCupsRetrievePpdRequest("printer"));

  EXPECT_TRUE(response.ppd().empty());
}

TEST(CupsToolTest, LpstatNoPrinter) {
  // Test the case where lpstat runs but doesn't return the printer we are
  // looking for.
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();

  const std::string printerName("test-printer");
  lptools->SetLpstatOutput("lpstat data not containing our printer name");

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cupsTool(remote.get());
  cupsTool.SetLpToolsForTesting(std::move(lptools));

  CupsRetrievePpdResponse response =
      cupsTool.RetrievePpd(ConstructCupsRetrievePpdRequest(printerName));

  EXPECT_TRUE(response.ppd().empty());
}

TEST(CupsToolTest, InvalidPPDTooSmall) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::vector<uint8_t> empty_ppd;

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  CupsAddManuallyConfiguredPrinterResponse response =
      cups.AddManuallyConfiguredPrinter(
          ConstructCupsAddManuallyConfiguredPrinterRequest("test", "ipp://",
                                                           empty_ppd));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_INVALID_PPD);
}

TEST(CupsToolTest, InvalidPPDBadGzip) {
  base::test::SingleThreadTaskEnvironment task_environment;

  // Make the PPD look like it's gzipped.
  std::vector<uint8_t> bad_ppd(kMinimalPPDContent.begin(),
                               kMinimalPPDContent.end());
  bad_ppd[0] = 0x1f;
  bad_ppd[1] = 0x8b;

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  CupsAddManuallyConfiguredPrinterResponse response =
      cups.AddManuallyConfiguredPrinter(
          ConstructCupsAddManuallyConfiguredPrinterRequest("test", "ipp://",
                                                           bad_ppd));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_INVALID_PPD);
}

TEST(CupsToolTest, InvalidPPDBadContents) {
  base::test::SingleThreadTaskEnvironment task_environment;

  // Corrupt a valid PPD so it won't validate.
  std::vector<uint8_t> bad_ppd(kMinimalPPDContent.begin(),
                               kMinimalPPDContent.end());
  bad_ppd[0] = 'X';
  bad_ppd[1] = 'Y';
  bad_ppd[2] = 'Z';

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsTestPPDResult(4);  // Typical failure exit code.

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddManuallyConfiguredPrinterResponse response =
      cups.AddManuallyConfiguredPrinter(
          ConstructCupsAddManuallyConfiguredPrinterRequest("test", "ipp://",
                                                           bad_ppd));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_INVALID_PPD);
}

TEST(CupsToolTest, ManualMissingURI) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::vector<uint8_t> good_ppd(kMinimalPPDContent.begin(),
                                kMinimalPPDContent.end());

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsTestPPDResult(0);  // Successful validation.

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddManuallyConfiguredPrinterResponse response =
      cups.AddManuallyConfiguredPrinter(
          ConstructCupsAddManuallyConfiguredPrinterRequest("test", /*uri=*/"",
                                                           good_ppd));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_BAD_URI);
}

TEST(CupsToolTest, ManualMissingName) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::vector<uint8_t> good_ppd(kMinimalPPDContent.begin(),
                                kMinimalPPDContent.end());

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsTestPPDResult(0);       // Successful validation.
  lptools->SetCupsUriHelperResult(true);  // URI validated.

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddManuallyConfiguredPrinterResponse response =
      cups.AddManuallyConfiguredPrinter(
          ConstructCupsAddManuallyConfiguredPrinterRequest(
              /*name=*/"", "ipp://127.0.0.1:631/ipp/print", good_ppd));
  EXPECT_EQ(response.result(), AddPrinterResult::ADD_PRINTER_RESULT_CUPS_FATAL);
}

TEST(CupsToolTest, ManualUnknownError) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::vector<uint8_t> good_ppd(kMinimalPPDContent.begin(),
                                kMinimalPPDContent.end());

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsTestPPDResult(0);       // Successful validation.
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(1);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddManuallyConfiguredPrinterResponse response =
      cups.AddManuallyConfiguredPrinter(
          ConstructCupsAddManuallyConfiguredPrinterRequest(
              "test", "ipp://127.0.0.1:631/ipp/print", good_ppd));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_LPADMIN_FAILURE);
}

TEST(CupsToolTest, ManualInvalidPpdDuringLpadmin) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::vector<uint8_t> good_ppd(kMinimalPPDContent.begin(),
                                kMinimalPPDContent.end());

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsTestPPDResult(0);       // Successful validation.
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(5);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddManuallyConfiguredPrinterResponse response =
      cups.AddManuallyConfiguredPrinter(
          ConstructCupsAddManuallyConfiguredPrinterRequest(
              "test", "ipp://127.0.0.1:631/ipp/print", good_ppd));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_INVALID_PPD);
}

TEST(CupsToolTest, ManualNotAutoConf) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::vector<uint8_t> good_ppd(kMinimalPPDContent.begin(),
                                kMinimalPPDContent.end());

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsTestPPDResult(0);       // Successful validation.
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(9);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddManuallyConfiguredPrinterResponse response =
      cups.AddManuallyConfiguredPrinter(
          ConstructCupsAddManuallyConfiguredPrinterRequest(
              "test", "ipp://127.0.0.1:631/ipp/print", good_ppd));
  EXPECT_EQ(response.result(), AddPrinterResult::ADD_PRINTER_RESULT_CUPS_FATAL);
}

TEST(CupsToolTest, ManualUnhandledError) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::vector<uint8_t> good_ppd(kMinimalPPDContent.begin(),
                                kMinimalPPDContent.end());

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsTestPPDResult(0);       // Successful validation.
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(100);         // Error code without CUPS equivalent.

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddManuallyConfiguredPrinterResponse response =
      cups.AddManuallyConfiguredPrinter(
          ConstructCupsAddManuallyConfiguredPrinterRequest(
              "test", "ipp://127.0.0.1:631/ipp/print", good_ppd));
  EXPECT_EQ(response.result(), AddPrinterResult::ADD_PRINTER_RESULT_CUPS_FATAL);
}

TEST(CupsToolTest, AutoMissingURI) {
  base::test::SingleThreadTaskEnvironment task_environment;

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest("test", /*uri=*/""));
  EXPECT_EQ(response.result(), AddPrinterResult::ADD_PRINTER_RESULT_CUPS_FATAL);
}

TEST(CupsToolTest, AutoMissingName) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(true);  // URI validated.

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          /*name=*/"", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(), AddPrinterResult::ADD_PRINTER_RESULT_CUPS_FATAL);
}

TEST(CupsToolTest, AutoUnreasonableUri) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(false);  // Unreasonable URI.

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          /*name=*/"", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_BAD_URI);
}

TEST(CupsToolTest, AddAutoConfiguredPrinter) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(true);  // URI validated.

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          "test", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(), AddPrinterResult::ADD_PRINTER_RESULT_SUCCESS);
}

TEST(CupsToolTest, AutoUnknownError) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(1);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          "test", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_AUTOCONF_FAILURE);
}

TEST(CupsToolTest, AutoFatalError) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(2);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          "test", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(), AddPrinterResult::ADD_PRINTER_RESULT_CUPS_FATAL);
}

TEST(CupsToolTest, AutoIoError) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(3);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          "test", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_IO_ERROR);
}

TEST(CupsToolTest, AutoMemoryAllocError) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(4);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          "test", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_MEMORY_ALLOC_ERROR);
}

TEST(CupsToolTest, AutoInvalidPpd) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(5);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          "test", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(), AddPrinterResult::ADD_PRINTER_RESULT_CUPS_FATAL);
}

TEST(CupsToolTest, AutoServerUnreachable) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(6);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          "test", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(), AddPrinterResult::ADD_PRINTER_RESULT_CUPS_FATAL);
}

TEST(CupsToolTest, AutoPrinterUnreachable) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(7);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          "test", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_PRINTER_UNREACHABLE);
}

TEST(CupsToolTest, AutoPrinterWrongResponse) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(8);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          "test", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_PRINTER_WRONG_RESPONSE);
}

TEST(CupsToolTest, AutoPrinterNotAutoConf) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetCupsUriHelperResult(true);  // URI validated.
  lptools->SetLpadminResult(9);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddAutoConfiguredPrinterResponse response = cups.AddAutoConfiguredPrinter(
      ConstructCupsAddAutoConfiguredPrinterRequest(
          "test", "ipp://127.0.0.1:631/ipp/print"));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_PRINTER_NOT_AUTOCONF);
}

TEST(CupsToolTest, FoomaticPPD) {
  base::test::SingleThreadTaskEnvironment task_environment;

  // Make the PPD look like it has a foomatic-rip filter.
  constexpr base::StringPiece kFoomaticLine(
      R"foo(*cupsFilter: "application/vnd.cups-pdf 0 foomatic-rip")foo");
  std::vector<uint8_t> foomatic_ppd(kMinimalPPDContent.begin(),
                                    kMinimalPPDContent.end());
  std::copy(kFoomaticLine.begin(), kFoomaticLine.end(),
            std::back_inserter(foomatic_ppd));

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetRunCommandResult(0);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  CupsAddManuallyConfiguredPrinterResponse response =
      cups.AddManuallyConfiguredPrinter(
          ConstructCupsAddManuallyConfiguredPrinterRequest("test", "ipp://",
                                                           foomatic_ppd));
  EXPECT_EQ(response.result(), AddPrinterResult::ADD_PRINTER_RESULT_SUCCESS);
}

TEST(CupsToolTest, FoomaticError) {
  base::test::SingleThreadTaskEnvironment task_environment;

  // Make the PPD look like it has a foomatic-rip filter.
  constexpr base::StringPiece kFoomaticLine(
      R"foo(*cupsFilter: "application/vnd.cups-pdf 0 foomatic-rip")foo");
  std::vector<uint8_t> foomatic_ppd(kMinimalPPDContent.begin(),
                                    kMinimalPPDContent.end());
  std::copy(kFoomaticLine.begin(), kFoomaticLine.end(),
            std::back_inserter(foomatic_ppd));

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();
  lptools->SetRunCommandResult(-1);

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));
  CupsAddManuallyConfiguredPrinterResponse response =
      cups.AddManuallyConfiguredPrinter(
          ConstructCupsAddManuallyConfiguredPrinterRequest("test", "ipp://",
                                                           foomatic_ppd));
  EXPECT_EQ(response.result(),
            AddPrinterResult::ADD_PRINTER_RESULT_CUPS_INVALID_PPD);
}

TEST(CupsToolTest, RemovePrinter) {
  base::test::SingleThreadTaskEnvironment task_environment;

  std::unique_ptr<FakeLpTools> lptools = std::make_unique<FakeLpTools>();

  MockExecutor mock_executor;
  mojo::Remote<mojom::Executor> remote;
  remote.Bind(mock_executor.pending_remote());
  CupsTool cups(remote.get());
  cups.SetLpToolsForTesting(std::move(lptools));

  // Our FakeLpTools always returns 0 for lpadmin calls, so we expect this to
  // pass.
  CupsRemovePrinterResponse response =
      cups.RemovePrinter(ConstructCupsRemovePrinterRequest("printer-name"));
  EXPECT_EQ(response.result(), true);
}

}  // namespace printscanmgr
