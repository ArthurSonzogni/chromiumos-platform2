// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_ROUTINES_DIAG_PROCESS_ADAPTER_IMPL_H_
#define DIAGNOSTICS_ROUTINES_DIAG_PROCESS_ADAPTER_IMPL_H_

#include <string>
#include <vector>

#include "diagnostics/routines/diag_process_adapter.h"

namespace diagnostics {

// Production implementation of DiagProcessAdapter.
class DiagProcessAdapterImpl final : public DiagProcessAdapter {
 public:
  DiagProcessAdapterImpl();
  ~DiagProcessAdapterImpl() override;
  base::TerminationStatus GetStatus(
      const base::ProcessHandle& handle) const override;
  bool StartProcess(const std::vector<std::string>& args,
                    base::ProcessHandle* handle) override;
  bool KillProcess(const base::ProcessHandle& handle) override;

 private:
  std::string exe_path_;

  DISALLOW_COPY_AND_ASSIGN(DiagProcessAdapterImpl);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_ROUTINES_DIAG_PROCESS_ADAPTER_IMPL_H_
