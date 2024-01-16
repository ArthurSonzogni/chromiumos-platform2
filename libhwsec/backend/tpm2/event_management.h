// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_EVENT_MANAGEMENT_H_
#define LIBHWSEC_BACKEND_TPM2_EVENT_MANAGEMENT_H_

#include <memory>
#include <string>
#include <utility>

#include <absl/container/flat_hash_set.h>

#include "libhwsec/backend/event_management.h"
#include "libhwsec/backend/tpm2/trunks_context.h"
#include "libhwsec/proxy/proxy.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/event.h"

namespace hwsec {

class EventManagementTpm2 : public EventManagement {
 public:
  EventManagementTpm2(TrunksContext& context,
                      MiddlewareDerivative& middleware_derivative)
      : context_(context), middleware_derivative_(middleware_derivative) {}

  ~EventManagementTpm2();

  StatusOr<ScopedEvent> Start(const std::string& event) override;
  Status Stop(const std::string& event) override;

 private:
  TrunksContext& context_;
  MiddlewareDerivative& middleware_derivative_;

  absl::flat_hash_set<std::string> events;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_EVENT_MANAGEMENT_H_
