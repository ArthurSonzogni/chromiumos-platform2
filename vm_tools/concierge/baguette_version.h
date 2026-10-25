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
constexpr char kBaguetteVersion[] = "2026-10-25-000117_1460ad55824a2e2bf6e3c197646dcce178fd94dd";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "19b50193d02cf86504d6fa4675f8925dc81c8ad77e2f36c935094caef9c929c4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "2a39969c8976e9eb507a8be6becd3b5a6c823598f38f38e55e364218735ae05b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
