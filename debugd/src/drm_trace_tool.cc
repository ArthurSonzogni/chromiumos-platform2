// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This tool is used for getting dmesg information through debugd.

#include "debugd/src/drm_trace_tool.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/files/safe_fd.h>
#include <debugd/src/error_utils.h>

namespace debugd {

namespace {
// Categories copied from drm_debug_category:
// https://elixir.bootlin.com/linux/v5.14.12/source/include/drm/drm_print.h#L277
// These are not exposed in libdrm or other userspace headers, so we need to
// duplicate them here.
enum DRMDebugCategories {
  DRM_UT_CORE = 0x01,
  DRM_UT_DRIVER = 0x02,
  DRM_UT_KMS = 0x04,
  DRM_UT_PRIME = 0x08,
  DRM_UT_ATOMIC = 0x10,
  DRM_UT_VBL = 0x20,
  DRM_UT_STATE = 0x40,
  DRM_UT_LEASE = 0x80,
  DRM_UT_DP = 0x100,
  DRM_UT_DRMRES = 0x200,
};

constexpr bool AreEqual(DRMDebugCategories c1, DRMTraceCategories c2) {
  typedef std::underlying_type<DRMDebugCategories>::type t1;
  typedef std::underlying_type<DRMTraceCategories>::type t2;
  return static_cast<t1>(c1) == static_cast<t2>(c2);
}

static_assert(AreEqual(DRM_UT_CORE, DRMTraceCategory_CORE));
static_assert(AreEqual(DRM_UT_DRIVER, DRMTraceCategory_DRIVER));
static_assert(AreEqual(DRM_UT_KMS, DRMTraceCategory_KMS));
static_assert(AreEqual(DRM_UT_PRIME, DRMTraceCategory_PRIME));
static_assert(AreEqual(DRM_UT_ATOMIC, DRMTraceCategory_ATOMIC));
static_assert(AreEqual(DRM_UT_VBL, DRMTraceCategory_VBL));
static_assert(AreEqual(DRM_UT_STATE, DRMTraceCategory_STATE));
static_assert(AreEqual(DRM_UT_LEASE, DRMTraceCategory_LEASE));
static_assert(AreEqual(DRM_UT_DP, DRMTraceCategory_DP));
static_assert(AreEqual(DRM_UT_DRMRES, DRMTraceCategory_DRMRES));

const uint32_t kDefaultMask = DRM_UT_DRIVER | DRM_UT_KMS | DRM_UT_DP;
const uint32_t kAllCategories = 0x3FF;
const uint32_t kDefaultTraceBufferSizeKb = 64;
// 2MB * num_cpus. This is somewhat arbitrary. Increase in size if we need more.
const uint32_t kDebugTraceBufferSizeKb = 2 * 1024;
// 256K, to account for large blocks of text such as modetest output.
const uint32_t kMaxLogSize = 256 * 1024;

// Drop the first slash since the root path can be set for testing.
const char kTraceMaskFile[] = "sys/module/drm/parameters/trace";
const char kTraceBufferSizeFile[] =
    "sys/kernel/debug/tracing/instances/drm/buffer_size_kb";
const char kTraceMarkerFile[] =
    "sys/kernel/debug/tracing/instances/drm/trace_marker";

const char kDRMTraceToolErrorString[] = "org.chromium.debugd.error.DRMTrace";

// Convert |size| to the corresponding DRMTraceSizes enum value. Returns
// false if |size| is not a valid enum value.
bool ConvertSize(uint32_t size, DRMTraceSizes* out_size) {
  if (size == DRMTraceSize_DEFAULT)
    *out_size = DRMTraceSize_DEFAULT;
  else if (size == DRMTraceSize_DEBUG)
    *out_size = DRMTraceSize_DEBUG;
  else
    return false;

  return true;
}
}  // namespace

DRMTraceTool::DRMTraceTool() : DRMTraceTool(base::FilePath("/")) {}

DRMTraceTool::DRMTraceTool(const base::FilePath& root_path)
    : root_path_(root_path) {}

bool DRMTraceTool::SetCategories(brillo::ErrorPtr* error, uint32_t categories) {
  base::FilePath mask_path = root_path_.Append(kTraceMaskFile);

  if (categories & ~kAllCategories) {
    DEBUGD_ADD_ERROR_FMT(error, kDRMTraceToolErrorString,
                         "Unknown category flags: 0x%x",
                         categories & ~kAllCategories);
    return false;
  }

  // Flags for categories match the flags expected by the kernel for drm_trace,
  // as asserted above.
  uint32_t mask = categories;
  if (categories == 0)
    mask = kDefaultMask;

  return WriteToFile(error, mask_path, base::StringPrintf("%d", mask));
}

bool DRMTraceTool::SetSize(brillo::ErrorPtr* error, uint32_t size_enum) {
  base::FilePath size_path = root_path_.Append(kTraceBufferSizeFile);

  DRMTraceSizes drm_trace_size;
  if (!ConvertSize(size_enum, &drm_trace_size)) {
    DEBUGD_ADD_ERROR_FMT(error, kDRMTraceToolErrorString,
                         "Invalid value for size: %u", size_enum);
    return false;
  }

  uint32_t size_kb = kDefaultTraceBufferSizeKb;
  if (drm_trace_size == DRMTraceSize_DEBUG)
    size_kb = kDebugTraceBufferSizeKb;

  return WriteToFile(error, size_path, base::StringPrintf("%d", size_kb));
}

bool DRMTraceTool::AnnotateLog(brillo::ErrorPtr* error,
                               const std::string& log) {
  base::FilePath marker_path = root_path_.Append(kTraceMarkerFile);

  // Ensure the string is a reasonable size.
  if (log.size() >= kMaxLogSize) {
    DEBUGD_ADD_ERROR(error, kDRMTraceToolErrorString, "Log too large.");
    return false;
  }

  // Sanitize the log. Allow only ascii printable characters and whitespace
  // (which will include newlines). Invalid characters will be replaced
  // with '_'.
  const char kReplacementChar = '_';
  std::string sanitized_log;
  sanitized_log.resize(log.size());
  std::replace_copy_if(
      log.begin(), log.end(), sanitized_log.begin(),
      [](auto c) {
        return !(base::IsAsciiPrintable(c) || base::IsAsciiWhitespace(c));
      },
      kReplacementChar);

  return WriteToFile(error, marker_path, sanitized_log);
}

bool DRMTraceTool::WriteToFile(brillo::ErrorPtr* error,
                               const base::FilePath& path,
                               const std::string& contents) {
  brillo::SafeFD::SafeFDResult result = brillo::SafeFD::Root();
  if (brillo::SafeFD::IsError(result.second)) {
    DEBUGD_ADD_ERROR_FMT(error, kDRMTraceToolErrorString,
                         "Failed to open SafeFD::Root(), error: %d",
                         result.second);
    return false;
  }

  result = result.first.OpenExistingFile(path);
  if (brillo::SafeFD::IsError(result.second)) {
    DEBUGD_ADD_ERROR_FMT(error, kDRMTraceToolErrorString,
                         "Failed to open %s, error: %d", path.value().c_str(),
                         result.second);
    return false;
  }

  brillo::SafeFD::Error safefd_error =
      result.first.Write(contents.c_str(), contents.size());
  if (brillo::SafeFD::IsError(safefd_error)) {
    DEBUGD_ADD_ERROR_FMT(error, kDRMTraceToolErrorString,
                         "Failed to write to %s, error: %d",
                         path.value().c_str(), safefd_error);
    return false;
  }

  return true;
}

}  // namespace debugd
