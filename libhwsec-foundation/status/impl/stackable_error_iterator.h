// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_STATUS_IMPL_STACKABLE_ERROR_ITERATOR_H_
#define LIBHWSEC_FOUNDATION_STATUS_IMPL_STACKABLE_ERROR_ITERATOR_H_

#include <type_traits>

#include "libhwsec-foundation/status/impl/stackable_error_forward_declarations.h"

namespace hwsec_foundation {
namespace status {
namespace __impl {

// Const iterator to the objects of the stack. The value of the iterator is a
// const pointer to the underlying error object.
template <typename _Et>
class StackableErrorConstIterator {
 private:
  // Internal iterator is an implementation detail clients should not know
  // about.
  using internal_iterator = typename StackHolderType<_Et>::const_iterator;

 public:
  // The type which dereferencing the iterator provides. It is a pointer to the
  // underlying error object, not the object itself.
  using value_type = typename StackPointerHolderType<_Et>::pointer;

  // Copy and move constructability is safe since the underlying iterator must
  // be copy/move constructible/assignable.
  StackableErrorConstIterator(const StackableErrorConstIterator&) noexcept =
      default;
  StackableErrorConstIterator& operator=(
      const StackableErrorConstIterator&) noexcept = default;
  StackableErrorConstIterator(StackableErrorConstIterator&&) noexcept = default;
  StackableErrorConstIterator& operator=(
      StackableErrorConstIterator&&) noexcept = default;

  // Value access interface - returns a reference to a const pointer to the
  // underlying error object.
  std::add_const_t<value_type> value() const {
    // We expect the value to be "uinique_ptr"-like object.
    return iter_->get();
  }

  // Iterator trais.

  std::add_const_t<value_type> operator*() const { return value(); }

  std::add_const_t<value_type> operator->() const { return value(); }

  StackableErrorConstIterator<_Et>& operator++() {
    ++iter_;
    return *this;
  }

  StackableErrorConstIterator<_Et>& operator++(int) {
    iter_++;
    return *this;
  }

 private:
  // Internal iterator must be explicitly supplied
  // Private to not allow explicit construction by clients.
  explicit StackableErrorConstIterator(internal_iterator iter) noexcept
      : iter_(iter) {}

  // Iterator for StackableError wraps the iterator to its backend.
  internal_iterator iter_;

  // Make range a friend to allow constructing the iterator from it.
  friend class StackableErrorConstRange<_Et>;

  // Make non-const iterator a friend to allow casting it const.
  friend class StackableErrorIterator<_Et>;

  // Make class introspector a friend to use it in comparisons.
  friend struct IntrospectStackableErrorIterator;
};

// Const iterator to the objects of the stack. The value of the iterator is a
// non-const pointer to the underlying error object.
template <typename _Et>
class StackableErrorIterator {
 private:
  // Internal iterator is an implementation detail clients should not know
  // about.
  using internal_iterator = typename StackHolderType<_Et>::iterator;

 public:
  // The type which dereferencing the iterator provides. It is a pointer to the
  // underlying error object, not the object itself.
  using value_type = typename StackPointerHolderType<_Et>::pointer;

  // Copy and move constructability is safe since the underlying iterator must
  // be copy/move constructible/assignable.
  StackableErrorIterator(const StackableErrorIterator&) noexcept = default;
  StackableErrorIterator& operator=(const StackableErrorIterator&) noexcept =
      default;
  StackableErrorIterator(StackableErrorIterator&&) noexcept = default;
  StackableErrorIterator& operator=(StackableErrorIterator&&) noexcept =
      default;

  // Non-const iterator must be castable to const version for assignment and
  // comparison.
  operator StackableErrorConstIterator<_Et>() noexcept {
    return StackableErrorConstIterator<_Et>(iter_);
  }

  // Value access interface - returns a reference to a non-const pointer to the
  // underlying error object.
  value_type value() const {
    // We expect the value to be "uinique_ptr"-like object.
    return iter_->get();
  }

  // Iterator trais.

  value_type operator*() const { return value(); }

  value_type operator->() const { return value(); }

  StackableErrorIterator<_Et>& operator++() {
    ++iter_;
    return *this;
  }

  StackableErrorIterator<_Et>& operator++(int) {
    iter_++;
    return *this;
  }

 private:
  // Iterator for StackableError wraps the iterator to its backend.
  internal_iterator iter_;

  // Internal iterator must be explicitly supplied.
  // Private to not allow explicit construction by clients.
  explicit StackableErrorIterator(internal_iterator iter) noexcept
      : iter_(iter) {}

  // Make range a friend to allow constructing the iterator from it.
  friend class StackableErrorRange<_Et>;

  // Make class introspector a friend to use it in comparisons.
  friend struct IntrospectStackableErrorIterator;
};

// Introspection function to use in comparators, so we need to provide only one
// friend to iterator class.
struct IntrospectStackableErrorIterator {
  template <typename _Et>
  auto operator()(const StackableErrorConstIterator<_Et>& it) noexcept {
    return it.iter_;
  }
  template <typename _Et>
  auto operator()(const StackableErrorIterator<_Et>& it) noexcept {
    return it.iter_;
  }
};

// Iterators must be comparable. We define only const comparison since non-const
// iterator can be cast to const.
// The following template construct allows us to generate all combinations of
// comparison operators in one go. It works as following:
// |_Ct1| and |_Ct2| are templated themselves and represent the "container".
// |_Et| is the element type of the container.
// |DisambiguationGuard| is an |ExplicitArgumentBarrier| idiom, but with the
// reference type specialized to disambiguate other broad templates like this.
// That is needed because the SFINAE guards can not disabmiguate broad templates
// against similarly formed (we have another instance of it in range object).
// Then the SFINAE |enable_if_t| cut any possible instantiation where the
// actual container type is not what we expect in the comparator.
template <template <typename> typename _Ct1,
          template <typename>
          typename _Ct2,
          typename _Et,
          StackableErrorIterator<_Et>&... DisambiguationGuard,
          typename = std::enable_if_t<
              std::is_same_v<_Ct1<_Et>, StackableErrorConstIterator<_Et>> ||
              std::is_same_v<_Ct1<_Et>, StackableErrorIterator<_Et>>>,
          typename = std::enable_if_t<
              std::is_same_v<_Ct2<_Et>, StackableErrorConstIterator<_Et>> ||
              std::is_same_v<_Ct2<_Et>, StackableErrorIterator<_Et>>>>
inline bool operator==(const _Ct1<_Et>& it1, const _Ct2<_Et>& it2) noexcept {
  return IntrospectStackableErrorIterator()(it1) ==
         IntrospectStackableErrorIterator()(it2);
}

template <template <typename> typename _Ct1,
          template <typename>
          typename _Ct2,
          typename _Et,
          StackableErrorIterator<_Et>&... DisambiguationGuard,
          typename = std::enable_if_t<
              std::is_same_v<_Ct1<_Et>, StackableErrorConstIterator<_Et>> ||
              std::is_same_v<_Ct1<_Et>, StackableErrorIterator<_Et>>>,
          typename = std::enable_if_t<
              std::is_same_v<_Ct2<_Et>, StackableErrorConstIterator<_Et>> ||
              std::is_same_v<_Ct2<_Et>, StackableErrorIterator<_Et>>>>
inline bool operator!=(const _Ct1<_Et>& it1, const _Ct2<_Et>& it2) noexcept {
  return !(it1 == it2);
}

}  // namespace __impl
}  // namespace status
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_STATUS_IMPL_STACKABLE_ERROR_ITERATOR_H_
