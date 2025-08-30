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
constexpr char kBaguetteVersion[] = "2025-08-30-000104_819a185109d9d6ca15a8a78277575f5d1fcb165b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4b13ebc845c0e64218cac8a25b0e0f1b411b6a9d43094384f24ee08cee6a0c0b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "828fc71aa0dc222acef9ebe7ff57cbef5d02523fb591a884c001d7401fc54f1d";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
