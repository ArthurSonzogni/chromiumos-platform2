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
constexpr char kBaguetteVersion[] = "2025-12-21-000117_dddf50645b62a7aab6c034d07bc29c022bf3e18e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "fb8b84d671b77408c8dae72ead73d1f2b8251428045c9039ba9144d40f3cee0b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "38671e262a1ad5a36ce725cf07545a4293ac8c9885f15fd1731a2e8cd44a60b6";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
