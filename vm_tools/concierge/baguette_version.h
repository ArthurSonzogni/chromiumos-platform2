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
constexpr char kBaguetteVersion[] = "2026-06-05-000103_d7bc8de18418f4e7f3dff4b776af256a0009e6b8";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "2ada396a9302fee4d7ded6b28409065863325875dfd598ecdcce74a76fec29f0";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b71bc64da6746bfaf414f4404ebe80940064280a499a20195058b15cb17ffe81";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
