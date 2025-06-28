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
constexpr char kBaguetteVersion[] = "2025-06-28-000122_97b745abe016781798cb34f84ec9718e24d0ce55";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "370bd15e60ba6d0fe5da4db308d9020a57c6f11fe429f123507af118b7df4e68";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "05b1e3a8b85290949ccaeafafb92c05593b36a9f1275026759d270e990c5ac12";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
