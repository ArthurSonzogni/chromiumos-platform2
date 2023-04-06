// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMINT_CONTEXT_ARC_KEYMINT_CONTEXT_H_
#define ARC_KEYMINT_CONTEXT_ARC_KEYMINT_CONTEXT_H_

#include <hardware/keymaster_defs.h>
#include <keymaster/authorization_set.h>
#include <keymaster/contexts/pure_soft_keymaster_context.h>
#include <keymaster/key.h>
#include <keymaster/key_factory.h>
#include <keymaster/UniquePtr.h>

namespace arc::keymint::context {

// Defines specific behavior for ARC KeyMint in ChromeOS.
class ArcKeyMintContext : public ::keymaster::PureSoftKeymasterContext {
 public:
  ArcKeyMintContext();
  ~ArcKeyMintContext() override;
  // Not copyable nor assignable.
  ArcKeyMintContext(const ArcKeyMintContext&) = delete;
  ArcKeyMintContext& operator=(const ArcKeyMintContext&) = delete;

 private:
  // TODO(b/274723555): Implement ARC KeyMint context.
  // mutable ContextAdaptor context_adaptor_;

  // mutable CrosKeyFactory rsa_key_factory_;

  // mutable std::vector<mojom::ChromeOsKeyPtr> placeholder_keys_;

  // friend class ContextTestPeer;
};

}  // namespace arc::keymint::context

#endif  // ARC_KEYMINT_CONTEXT_ARC_KEYMINT_CONTEXT_H_
