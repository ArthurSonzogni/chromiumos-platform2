// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mount_encrypted/mount_encrypted_metrics.h"

#include <base/check_op.h>
#include <base/time/time.h>

namespace mount_encrypted {

namespace {
static MountEncryptedMetrics* g_metrics = nullptr;

constexpr char kSystemKeyStatus[] = "Platform.MountEncrypted.SystemKeyStatus";
constexpr char kEncryptionKeyStatus[] =
    "Platform.MountEncrypted.EncryptionKeyStatus";

}  // namespace

MountEncryptedMetrics::MountEncryptedMetrics(const std::string& output_file) {
  metrics_library_.SetOutputFile(output_file);
}

// static
void MountEncryptedMetrics::Initialize(const std::string& output_file) {
  CHECK(!g_metrics);
  g_metrics = new MountEncryptedMetrics(output_file);
}

// static
MountEncryptedMetrics* MountEncryptedMetrics::Get() {
  CHECK(g_metrics);
  return g_metrics;
}

// static
void MountEncryptedMetrics::Reset() {
  CHECK(g_metrics);
  delete g_metrics;
  g_metrics = nullptr;
}

void MountEncryptedMetrics::ReportSystemKeyStatus(
    EncryptionKey::SystemKeyStatus status) {
  metrics_library_.SendEnumToUMA(
      kSystemKeyStatus, static_cast<int>(status),
      static_cast<int>(EncryptionKey::SystemKeyStatus::kCount));
}

void MountEncryptedMetrics::ReportEncryptionKeyStatus(
    EncryptionKey::EncryptionKeyStatus status) {
  metrics_library_.SendEnumToUMA(
      kEncryptionKeyStatus, static_cast<int>(status),
      static_cast<int>(EncryptionKey::EncryptionKeyStatus::kCount));
}

}  // namespace mount_encrypted
