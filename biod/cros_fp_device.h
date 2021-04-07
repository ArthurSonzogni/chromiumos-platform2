// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef BIOD_CROS_FP_DEVICE_H_
#define BIOD_CROS_FP_DEVICE_H_

#include <bitset>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <libec/ec_command_factory.h>
#include <libec/fingerprint/fp_info_command.h>
#include <libec/fingerprint/fp_mode.h>

#include "biod/biod_metrics.h"
#include "biod/cros_fp_device_interface.h"
#include "biod/uinput_device.h"

namespace biod {

class CrosFpDevice : public CrosFpDeviceInterface {
 public:
  static std::unique_ptr<CrosFpDevice> Create(
      BiodMetricsInterface* biod_metrics,
      std::unique_ptr<ec::EcCommandFactoryInterface> ec_command_factory) {
    // Using new to access non-public constructor.
    // See https://abseil.io/tips/134.
    auto dev = base::WrapUnique(
        new CrosFpDevice(biod_metrics, std::move(ec_command_factory)));
    if (!dev->Init()) {
      return nullptr;
    }
    return dev;
  }

  void SetMkbpEventCallback(MkbpCallback callback) override;

  // Run a simple command to get the version information from FP MCU and check
  // whether the image type returned is the same as |expected_image|.
  static bool WaitOnEcBoot(const base::ScopedFD& cros_fp_fd,
                           ec_current_image expected_image);

  // Run a simple command to get the version information from FP MCU.
  static base::Optional<EcVersion> GetVersion(const base::ScopedFD& cros_fp_fd);

  // CrosFpDeviceInterface overrides:
  ~CrosFpDevice() override;

  bool SetFpMode(const ec::FpMode& mode) override;
  ec::FpMode GetFpMode() override;
  base::Optional<FpStats> GetFpStats() override;
  base::Optional<std::bitset<32>> GetDirtyMap() override;
  bool SupportsPositiveMatchSecret() override;
  base::Optional<brillo::SecureVector> GetPositiveMatchSecret(
      int index) override;
  std::unique_ptr<VendorTemplate> GetTemplate(int index) override;
  bool UploadTemplate(const VendorTemplate& tmpl) override;
  bool SetContext(std::string user_id) override;
  bool ResetContext() override;
  // Initialise the entropy in the SBP. If |reset| is true, the old entropy
  // will be deleted. If |reset| is false, we will only add entropy, and only
  // if no entropy had been added before.
  bool InitEntropy(bool reset) override;
  bool UpdateFpInfo() override;

  int MaxTemplateCount() override;
  int TemplateVersion() override;
  int DeadPixelCount() override;

  ec::EcCmdVersionSupportStatus EcCmdVersionSupported(uint16_t cmd,
                                                      uint32_t ver) override;

  // Kernel device exposing the MCU command interface.
  static constexpr char kCrosFpPath[] = "/dev/cros_fp";

  // Although very rare, we have seen device commands fail due
  // to ETIMEDOUT. For this reason, we attempt certain critical
  // device IO operation twice.
  static constexpr int kMaxIoAttempts = 2;

  static constexpr int kLastTemplate = -1;

 protected:
  CrosFpDevice(
      BiodMetricsInterface* biod_metrics,
      std::unique_ptr<ec::EcCommandFactoryInterface> ec_command_factory)
      : ec_command_factory_(std::move(ec_command_factory)),
        biod_metrics_(biod_metrics) {}

  bool Init();

  virtual int read(int fd, void* buf, size_t count) {
    return ::read(fd, buf, count);
  }

  base::Optional<std::string> ReadVersion();

 private:
  struct EcProtocolInfo {
    uint16_t max_read = 0;
    uint16_t max_write = 0;
  };

  bool EcDevInit();
  base::Optional<EcProtocolInfo> EcProtoInfo();
  bool EcReboot(ec_current_image to_image);
  // Run the EC command to generate new entropy in the underlying MCU.
  // |reset| specifies whether we want to merely add entropy (false), or
  // perform a reset, which erases old entropy(true).
  bool AddEntropy(bool reset);
  // Get block id from rollback info.
  base::Optional<int32_t> GetRollBackInfoId();
  base::Optional<brillo::SecureVector> FpReadMatchSecret(uint16_t index);
  base::Optional<int> GetIndexOfLastTemplate();
  // Run a sequence of EC commands to update the entropy in the
  // MCU. If |reset| is set to true, it will additionally erase the existing
  // entropy too.
  bool UpdateEntropy(bool reset);
  std::unique_ptr<struct ec_response_flash_protect> GetFlashProtect() const;

  void OnEventReadable();

  base::ScopedFD cros_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
  EcProtocolInfo ec_protocol_info_;
  std::unique_ptr<ec::FpInfoCommand> info_;

  std::unique_ptr<ec::EcCommandFactoryInterface> ec_command_factory_;
  MkbpCallback mkbp_event_;
  UinputDevice input_device_;

  BiodMetricsInterface* biod_metrics_ = nullptr;  // Not owned.
};

}  // namespace biod

#endif  // BIOD_CROS_FP_DEVICE_H_
