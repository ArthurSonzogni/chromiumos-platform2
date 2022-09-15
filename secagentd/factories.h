// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_FACTORIES_H_
#define SECAGENTD_FACTORIES_H_

#include <memory>

#include "base/memory/scoped_refptr.h"
#include "secagentd/plugins.h"

namespace secagentd {

class BpfPluginFactoryInterface {
 public:
  virtual std::unique_ptr<PluginInterface> CreateProcessPlugin(
      scoped_refptr<MessageSender> message_sender) = 0;
  virtual ~BpfPluginFactoryInterface() = default;
};

class BpfPluginFactory : public BpfPluginFactoryInterface {
 public:
  std::unique_ptr<PluginInterface> CreateProcessPlugin(
      scoped_refptr<MessageSender> message_sender) override;
};

}  // namespace secagentd

#endif  // SECAGENTD_FACTORIES_H_
