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
constexpr char kBaguetteVersion[] = "2026-02-04-000100_943cc228d285f089d62ced0b6ae63d741b6ce184";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "0d89aa423e2eb8cac4df6d899deafbf0d80f51d68ad44c4d7dcd6c3eb2c9e423";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5b1e2cd15dff304aaf8eaf7f0d029c3ba8bbdddd20dfd9466f7123e887fee9df";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
