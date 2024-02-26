// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_THROTTLER_H_
#define SHILL_THROTTLER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>

#include "shill/callbacks.h"
#include "shill/tc_process.h"

namespace shill {

// The Throttler class implements bandwidth throttling for inbound/outbound
// traffic, using Linux's 'traffic control'(tc) tool from the iproute2 code.
// (https://wiki.linuxfoundation.org/networking/iproute2).
// A detailed introduction to traffic control using tc is available at
// http://lartc.org/howto/
// This solution makes use of two queueing disciplines ('qdisc's),
// one each for ingress (inbound) and egress (outbound) traffic and
// a policing filter on the ingress side.
// Any inbound traffic above a rate of ${DLRATE} kbits/s is dropped on the
// floor. For egress (upload) traffic, a qdisc using the Hierarchical Token
// Bucket algorithm is used.
class Throttler {
 public:
  explicit Throttler(std::unique_ptr<TCProcessFactory> tc_process_factory =
                         std::make_unique<TCProcessFactory>());

  virtual ~Throttler();

  // Disables the throttling on |interfaces|.
  virtual bool DisableThrottlingOnAllInterfaces(
      ResultCallback callback, const std::vector<std::string>& interfaces);

  // Throttles the |interfaces| with upload/download bitrates. At least one of
  // |upload_rate_kbits| or |download_rate_kbits| should be non-zero.
  virtual bool ThrottleInterfaces(ResultCallback callback,
                                  uint32_t upload_rate_kbits,
                                  uint32_t download_rate_kbits,
                                  const std::vector<std::string>& interfaces);

  // Throttles the new interface with the upload/download bitrates from the
  // previous ThrottleInterfaces().
  // Returns false and do nothing if ThrottleInterfaces() hasn't been called, or
  // the bitrate is reset by DisableThrottlingOnAllInterfaces().
  virtual bool ApplyThrottleToNewInterface(const std::string& interface);

 private:
  // Throttles the next pending interface.
  void ThrottleNextPendingInterface();

  // Starts a TC process with the commands.
  bool StartTCProcess(const std::vector<std::string>& commands);
  // Called when the TC process has been exit.
  void OnTCProcessExited(int exit_status);

  // Resets the internal state and replies the result by |callback_|.
  void ResetAndReply(Error::Type error_type, std::string_view message);

  // The callback to return the result of the methods. The value is not null if
  // and only if the throttling task or the disabling task is running.
  ResultCallback callback_;

  // The upload/download bitrates.
  uint32_t upload_rate_kbits_ = 0;
  uint32_t download_rate_kbits_ = 0;

  // The pending interfaces to be throttled.
  std::vector<std::string> pending_throttled_interfaces_;

  // The TC process and its factory.
  std::unique_ptr<TCProcessFactory> tc_process_factory_;
  std::unique_ptr<TCProcess> tc_process_;

  base::WeakPtrFactory<Throttler> weak_ptr_factory_{this};
};

}  // namespace shill

#endif  // SHILL_THROTTLER_H_
