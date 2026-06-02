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
constexpr char kBaguetteVersion[] = "2026-06-02-000506_74a57afa76071c6580e29d6dafcde3f94174bf45";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f064bb68aacdf44c7665968693f61d7ee25afd0c0cffe8911dd9228abea3615e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "1c52a785e370bdd83cb062983b79819fad00888d8a57a2ca6bff24680c6ca13f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
