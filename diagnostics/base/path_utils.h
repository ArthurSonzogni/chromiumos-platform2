// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BASE_PATH_UTILS_H_
#define DIAGNOSTICS_BASE_PATH_UTILS_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

#include "diagnostics/base/file_utils.h"

namespace diagnostics {

// A helper class to define constexpr paths with concatenation supports. Use
// `MakePathLiteral` helper function to create literals.
// E.g.
//  constexpr auto kMyPath = MakePathLiteral("a", "b", "c");
//  constexpr auto kMyFile = MakePathLiteral(kMyPath, "myfile");
//
template <std::size_t Size>
class StaticPathLiteral;

class BRILLO_EXPORT PathLiteral {
 public:
  PathLiteral(const char** tokens, std::size_t size);

  template <std::size_t Size>
  PathLiteral(StaticPathLiteral<Size> path);  // NOLINT(runtime/explicit)

  PathLiteral(const PathLiteral&) = delete;
  PathLiteral& operator=(const PathLiteral&) = delete;

  ~PathLiteral();

  // Returns relative paths by join each tokens.
  base::FilePath ToPath() const;
  // Same as above but return std::string.
  std::string ToStr() const;

 private:
  std::vector<const char*> tokens_;
};

template <std::size_t Size>
class StaticPathLiteral {
  static_assert(Size >= 1);

 public:
  constexpr explicit StaticPathLiteral(
      const std::array<const char*, Size>& tokens)
      : tokens_(tokens) {}

  StaticPathLiteral(const StaticPathLiteral&) = default;
  StaticPathLiteral& operator=(const StaticPathLiteral&) = default;

  ~StaticPathLiteral() = default;

  constexpr std::array<const char*, Size> tokens() const { return tokens_; }

  base::FilePath ToPath() const { return PathLiteral(*this).ToPath(); }

  std::string ToStr() const { return PathLiteral(*this).ToStr(); }

 private:
  std::array<const char*, Size> tokens_;
};

template <std::size_t Size>
PathLiteral::PathLiteral(StaticPathLiteral<Size> path)
    : PathLiteral(path.tokens().data(), path.tokens().size()) {}

template <std::size_t Size>
constexpr auto MakePathLiteral(const std::array<const char*, Size>& base) {
  return StaticPathLiteral<Size>(base);
}

template <std::size_t Size, typename... Types>
constexpr auto MakePathLiteral(const std::array<const char*, Size>& base,
                               const char* token,
                               Types... rest) {
  std::array<const char*, Size + 1> res;
  std::copy(base.begin(), base.end(), res.begin());
  res.back() = token;
  return MakePathLiteral(res, rest...);
}

template <std::size_t S1, std::size_t S2, typename... Types>
constexpr auto MakePathLiteral(const std::array<const char*, S1>& base,
                               const StaticPathLiteral<S2>& path,
                               Types... rest) {
  std::array<const char*, S1 + S2> res;
  std::copy(base.begin(), base.end(), res.begin());
  std::array<const char*, S2> tokens = path.tokens();
  std::copy(tokens.begin(), tokens.end(), res.begin() + S1);
  return MakePathLiteral(res, rest...);
}

template <std::size_t Size, typename... Types>
constexpr auto MakePathLiteral(const StaticPathLiteral<Size>& base,
                               Types... rest) {
  return MakePathLiteral(base.tokens(), rest...);
}

template <typename... Types>
constexpr auto MakePathLiteral(const char* token, Types... rest) {
  std::array<const char*, 1> base;
  base[0] = token;
  return MakePathLiteral(base, rest...);
}

}  // namespace diagnostics

#endif  // DIAGNOSTICS_BASE_PATH_UTILS_H_
