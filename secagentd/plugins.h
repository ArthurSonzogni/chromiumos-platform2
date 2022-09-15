// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_PLUGINS_H_
#define SECAGENTD_PLUGINS_H_

#include <memory>
#include <string>

#include <absl/status/status.h>

#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/message_sender.h"
#include "secagentd/skeleton_factory.h"

namespace secagentd {
class PluginInterface {
 public:
  // True if the device policy indicates that this BpfPlugin should be loaded.
  virtual bool PolicyIsEnabled() const = 0;
  // Load the BPF into the kernel and attach it and then start collecting
  // events.
  virtual absl::Status LoadAndRun() = 0;
  virtual std::string GetPluginName() const = 0;
  virtual ~PluginInterface() = default;
};

class ProcessPlugin : public PluginInterface {
 public:
  ProcessPlugin(std::unique_ptr<BpfSkeletonFactoryInterface> factory,
                scoped_refptr<MessageSender> message_sender);
  bool PolicyIsEnabled() const override;
  absl::Status LoadAndRun() override;
  std::string GetPluginName() const override;
  void HandleRingBufferEvent(const bpf::event& bpf_event) const;
  void HandleBpfRingBufferReadReady() const;

 private:
  // This is static because it must be accessible to a C style function.
  static struct BpfCallbacks callbacks_;
  base::WeakPtrFactory<ProcessPlugin> weak_ptr_factory_;
  scoped_refptr<MessageSender> message_sender_;
  std::unique_ptr<BpfSkeletonFactoryInterface> factory_;
  std::unique_ptr<BpfSkeletonInterface> skeleton_wrapper_;
};

}  // namespace secagentd
#endif  // SECAGENTD_PLUGINS_H_
