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
constexpr char kBaguetteVersion[] = "2026-06-27-000103_02fbb11247d4a942f624ad2aea24f22dcf53303f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b69f74abdcd7881f70f8a0c1214daf7f4bdb3ca6e80c0789ba0c2cbec1a5974a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "68ec0be17b8e3aa035af12b6bb51be13aee4ff521218b5b2df5c34ffb47c1bce";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
