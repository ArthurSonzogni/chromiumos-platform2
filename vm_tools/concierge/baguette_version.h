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
constexpr char kBaguetteVersion[] = "2026-11-17-000126_a8fb9cfcdfe4adb265ce35dbc9b91a32959642ba";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "221777037fdac0b49be246d8d1da303d28eafd00f77c274eb65d73f5bc79a5c4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "518a88a87748eed595f6e93ee1bea568cbfa86260c0aa679fd6893b2e23caaab";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
