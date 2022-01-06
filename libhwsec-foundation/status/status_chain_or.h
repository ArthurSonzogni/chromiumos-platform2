// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_OR_H_
#define LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_OR_H_

#include <memory>
#include <utility>
#include <variant>

#include "libhwsec-foundation/status/status_chain.h"

// Convenience class to represent value or non-ok status.

namespace hwsec_foundation {
namespace status {

template <typename _Vt, typename _Et>
class StatusChainOr {
 public:
  using value_type = _Vt;
  using status_type = StatusChain<_Et>;
  using container_type = std::variant<value_type, status_type>;

  // We don't want default constructor.
  StatusChainOr() = delete;

  // We don't want copy constructor.
  StatusChainOr(const StatusChainOr&) = delete;

  // Accept the move constructor.
  StatusChainOr(StatusChainOr&& status_or)
      : value_(std::move(status_or.value_)) {}

  // Implicit conversion to StatusChainOr to allow transparent "return"s.
  template <
      typename _Ut,
      typename = std::enable_if_t<std::is_constructible_v<value_type, _Ut>>>
  StatusChainOr(_Ut&& v)  // NOLINT(runtime/explicit)
      : value_(container_type{std::in_place_type<value_type>,
                              std::forward<_Ut>(v)}) {}

  // Constructs the inner value in-place using the provided args, using the
  // `value_type(args...)` constructor.
  template <
      typename... Args,
      typename = std::enable_if_t<std::is_constructible_v<value_type, Args...>>>
  StatusChainOr(std::in_place_t, Args&&... args)  // NOLINT(runtime/explicit)
      : value_(container_type{std::in_place_type<value_type>,
                              std::forward<Args>(args)...}) {}

  // Constructs the inner value in-place using the provided args, using the
  // `value_type(args...)` constructor.
  template <typename _Ut,
            typename... Args,
            typename = std::enable_if_t<
                std::is_constructible_v<value_type,
                                        std::initializer_list<_Ut>,
                                        Args...>>>
  StatusChainOr(std::in_place_t,
                std::initializer_list<_Ut> ilist,
                Args&&... args)  // NOLINT(runtime/explicit)
      : value_(container_type{std::in_place_type<value_type>, ilist,
                              std::forward<Args>(args)...}) {}

  // Converting move constructor from a compatible stackable error type. It is
  // fine, since our internal stack representation is of a base |Base| type
  // anyway. SFINAE checks that the supplied pointer type is compatible with
  // this object's head type. Since manually specializing the operator could
  // lead to breaking invariant of the head object being castable to class
  // template type |_Et|, we use |ExplicitArgumentBarrier| idiom to make |_Ut|
  // auto-deducible only.
  template <int&... ExplicitArgumentBarrier,
            typename _Ut,
            typename = std::enable_if_t<
                std::is_convertible_v<_Ut*,
                                      typename StatusChain<_Et>::pointer>>>
  StatusChainOr(StatusChain<_Ut>&& other)  // NOLINT(runtime/explicit)
      : value_(
            container_type{std::in_place_type<status_type>, std::move(other)}) {
    CHECK(!std::get<status_type>(value_).ok())
        << " StatusChainOr cannot hold an OK status";
  }

  // We don't want copy operator.
  StatusChainOr& operator=(const StatusChainOr&) = delete;

  StatusChainOr& operator=(StatusChainOr&& status_or) noexcept {
    value_ = std::move(status_or.value_);
    return *this;
  }

  value_type* operator->() noexcept {
    CHECK(ok()) << " Arrow operator on a non-OK StatusChainOr is not allowed";
    return std::get_if<value_type>(&value_);
  }

  constexpr const value_type& operator*() const& {
    CHECK(ok()) << " Dereferencing a non-OK StatusChainOr is not allowed";
    return *std::get_if<value_type>(&value_);
  }

  value_type& operator*() & {
    CHECK(ok()) << " Dereferencing a non-OK StatusChainOr is not allowed";
    return *std::get_if<value_type>(&value_);
  }

  value_type&& operator*() && {
    CHECK(ok()) << " Dereferencing a non-OK StatusChainOr is not allowed";
    return std::move(*std::get_if<value_type>(&value_));
  }

  constexpr const value_type& value() const& noexcept {
    CHECK(ok()) << " Get the value of a non-OK StatusChainOr is not allowed";
    return *std::get_if<value_type>(&value_);
  }

  value_type& value() & noexcept {
    CHECK(ok()) << " Get the value of a non-OK StatusChainOr is not allowed";
    return *std::get_if<value_type>(&value_);
  }

  value_type&& value() && noexcept {
    CHECK(ok()) << " Get the value of a non-OK StatusChainOr is not allowed";
    return std::move(*std::get_if<value_type>(&value_));
  }

  bool ok() const noexcept {
    return std::holds_alternative<value_type>(value_);
  }

  constexpr const status_type& status() const& noexcept {
    if (ok()) {
      return ConstRefOkStatus<_Et>();
    }
    return *std::get_if<status_type>(&value_);
  }

  status_type status() && noexcept {
    if (ok()) {
      return OkStatus<_Et>();
    }
    return std::move(*std::get_if<status_type>(&value_));
  }

 private:
  container_type value_;
};

}  // namespace status
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_STATUS_STATUS_CHAIN_OR_H_
