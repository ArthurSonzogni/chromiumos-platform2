// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_FAKE_PROCESS_CONTROL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_FAKE_PROCESS_CONTROL_H_

#include <string>

#include <base/files/file.h>
#include <base/files/platform_file.h>
#include <base/files/scoped_temp_dir.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"

namespace diagnostics {

class FakeProcessControl : public ash::cros_healthd::mojom::ProcessControl {
 public:
  FakeProcessControl();
  FakeProcessControl(const FakeProcessControl& oth) = delete;
  FakeProcessControl(FakeProcessControl&& oth) = delete;
  ~FakeProcessControl() override = default;

  // ash::cros_healthd::mojom::ProcessControl overrides
  void GetStdout(GetStdoutCallback callback) override;
  void GetStderr(GetStderrCallback callback) override;
  void GetReturnCode(GetReturnCodeCallback callback) override;

  // Setters for the FakeProcessControlAttributes
  void SetStdoutFileContent(const std::string& stdout_content);
  void SetStderrFileContent(const std::string& stderr_content);
  void SetReturnCode(int return_code);

  // Binds a pending receiver to this object.
  void BindReceiver(
      mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl> receiver);

 private:
  // The return code of the process.
  int return_code_;
  // The file descriptor to read fake stdout output from.
  base::ScopedPlatformFile stdout_fd_;
  // The file descriptor to read fake stderr output from.
  base::ScopedPlatformFile stderr_fd_;
  // The temporary directory which files are stored in.
  base::ScopedTempDir temp_dir_;

  // The mojo receiver FakeProcessControl binds to.
  mojo::Receiver<ash::cros_healthd::mojom::ProcessControl> receiver_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_FAKE_PROCESS_CONTROL_H_
