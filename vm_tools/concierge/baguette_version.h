// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
#define VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

// This constant points to the image downloaded for new installations of
// Baguette.
// TODO(crbug.com/393151776): Point to luci recipe and builders that update this
// URL when new images are available.

// clang-format off
constexpr char kBaguetteVersion[] = "2025-08-13-000105_6538568dde4b4c62ced93cbb39eb20de977ef81f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "cc285309a583c4dc5f8cf0dda7c905a404d56b6804ef6b718dbf2fb17ea2cbf1";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a85060f99e367b3c2520d216784ad00c6c5259edad1a4561ad42f1e180a738a8";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
