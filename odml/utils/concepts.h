// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_UTILS_CONCEPTS_H_
#define ODML_UTILS_CONCEPTS_H_

namespace odml {

template <typename T>
concept Dereferencable = requires(T t) { *t; };

}  // namespace odml

#endif  // ODML_UTILS_CONCEPTS_H_
