// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/gsc_collector.h"

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <brillo/process/process.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/gsc_collector_base.h"

using brillo::ProcessImpl;

namespace {

const char kGscFirmwarePath[] = "/opt/google/ti50/firmware";
const char kGscToolPath[] = "/usr/sbin/gsctool";

// Ti50 crash log signature offset and length within |--clog| output, in bytes.
// Note that |--clog| outputs a string of hex values, with 2 chars per byte.
// https://b.corp.google.com/issues/265310865#comment40
// The 24 bytes starting at offset 40 in the crash dump can be used as a crash
// signature for UMA.
const int kCharsPerByte = 2;
const size_t kTi50SignatureStringOffset = 40 * kCharsPerByte;
const size_t kTi50SignatureStringSize = 24 * kCharsPerByte;

static bool IsTi50() {
  return base::PathExists(base::FilePath(kGscFirmwarePath));
}

}  // namespace

GscCollector::GscCollector(
    const scoped_refptr<
        base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
        metrics_lib)
    : GscCollectorBase(metrics_lib) {}

GscCollectorBase::Status GscCollector::GetTi50Flog(std::string* flog_output) {
  ProcessImpl gsctool;
  gsctool.AddArg(kGscToolPath);
  gsctool.AddArg("-a");           // spi/i2c AP-to-GSC interface
  gsctool.AddArg("--dauntless");  // Communicate with Dauntless chip.
  gsctool.AddArg("--flog");       // Retrieve contents of the flash log
  // Combine stdout and stderr.
  gsctool.RedirectOutputToMemory(true);

  const int result = gsctool.Run();
  *flog_output = gsctool.GetOutputString(STDOUT_FILENO);
  if (result != 0) {
    LOG(ERROR) << "Failed to get Ti50 gsctool flash log output. Error: '"
               << result << "'";
    return Status::Fail;
  }

  return Status::Success;
}

GscCollectorBase::Status GscCollector::GetGscFlog(std::string* flog_output) {
  if (IsTi50()) {
    return GetTi50Flog(flog_output);
  }

  // TODO(b/291127335): Update with better language.
  LOG(INFO) << "Unsupported GSC present on board. Unable to query GSC crashes.";
  return Status::Fail;
}

GscCollectorBase::Status GscCollector::GetTi50Clog(std::string* clog_output) {
  ProcessImpl gsctool;
  gsctool.AddArg(kGscToolPath);
  gsctool.AddArg("-a");           // spi/i2c AP-to-GSC interface
  gsctool.AddArg("--dauntless");  // Communicate with Dauntless chip.
  gsctool.AddArg("--clog");       // Retrieve contents of the crash log
  // Combine stdout and stderr.
  gsctool.RedirectOutputToMemory(true);

  const int result = gsctool.Run();
  *clog_output = gsctool.GetOutputString(STDOUT_FILENO);
  if (result != 0) {
    LOG(ERROR) << "Failed to get Ti50 gsctool crash log output. Error: '"
               << result << "'";
    return Status::Fail;
  }

  return Status::Success;
}

GscCollectorBase::Status GscCollector::GetGscClog(std::string* clog_output) {
  if (IsTi50()) {
    return GetTi50Clog(clog_output);
  }

  // TODO(b/291127335): Update with better language.
  LOG(INFO)
      << "Unsupported GSC present on board. Unable to query GSC crash log.";
  return Status::Fail;
}

GscCollectorBase::Status GscCollector::GetGscCrashSignatureOffsetAndLength(
    size_t* offset_out, size_t* size_out) {
  if (IsTi50()) {
    *offset_out = kTi50SignatureStringOffset;
    *size_out = kTi50SignatureStringSize;
    return Status::Success;
  }

  // TODO(b/291127335): Update with better language.
  LOG(INFO) << "Unsupported GSC present on board. No crash signature "
               "offset/size specified.";
  return Status::Fail;
}
