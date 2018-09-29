// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_DIAGNOSTICSD_DIAGNOSTICSD_CORE_DELEGATE_IMPL_H_
#define DIAGNOSTICS_DIAGNOSTICSD_DIAGNOSTICSD_CORE_DELEGATE_IMPL_H_

#include <memory>

#include <base/macros.h>
#include <brillo/daemons/daemon.h>

#include "diagnostics/diagnosticsd/diagnosticsd_core.h"

namespace diagnostics {

// Production implementation of DiagnosticsdCore's delegate.
class DiagnosticsdCoreDelegateImpl final : public DiagnosticsdCore::Delegate {
 public:
  explicit DiagnosticsdCoreDelegateImpl(brillo::Daemon* daemon);
  ~DiagnosticsdCoreDelegateImpl() override;

  // DiagnosticsdCore::Delegate overrides:
  std::unique_ptr<mojo::Binding<MojomDiagnosticsdService>>
  BindDiagnosticsdMojoService(MojomDiagnosticsdService* mojo_service,
                              base::ScopedFD mojo_pipe_fd) override;
  void BeginDaemonShutdown() override;

 private:
  // Unowned. The daemon must outlive this instance.
  brillo::Daemon* const daemon_;

  DISALLOW_COPY_AND_ASSIGN(DiagnosticsdCoreDelegateImpl);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_DIAGNOSTICSD_DIAGNOSTICSD_CORE_DELEGATE_IMPL_H_
