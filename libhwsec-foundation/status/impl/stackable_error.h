// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_STATUS_IMPL_STACKABLE_ERROR_H_
#define LIBHWSEC_FOUNDATION_STATUS_IMPL_STACKABLE_ERROR_H_

#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <base/check_op.h>

#include "libhwsec-foundation/status/impl/stackable_error_forward_declarations.h"

#include "libhwsec-foundation/status/impl/stackable_error_range.h"

namespace hwsec_foundation {
namespace status {
namespace __impl {

namespace RTTI {

// Check if an error pointer is of a certain type. Uses RTTI and relies on
// 'one-definition' rule to work correctly.
template <typename _Dt>
static bool Is(Error* error) {
  _Dt* cast = dynamic_cast<_Dt*>(error);
  return (cast != nullptr);
}

// Const overload for |Is<_Dt>|
template <typename _Dt>
static bool Is(const Error* error) {
  const _Dt* cast = dynamic_cast<const _Dt*>(error);
  return (cast != nullptr);
}

// Converts the pointer a certain type. Uses RTTI and relies on
// 'one-definition' rule to work correctly. Returns error if casting fails.
template <typename _Dt>
static _Dt* Cast(Error* error) {
  return dynamic_cast<_Dt*>(error);
}

// Const overload for |Cast<_Dt>|
template <typename _Dt>
static const _Dt* Cast(const Error* error) {
  return dynamic_cast<const _Dt*>(error);
}

}  // namespace RTTI

// Type trait checkers to determine if the class, intended to use with the
// status chain, is well-formed.
// TODO(dlunev): add a trait to verify callability of MakeStatusTrait.

template <typename, typename = void>
struct has_make_status_trait : public std::false_type {};
template <typename _Et>
struct has_make_status_trait<_Et, std::void_t<typename _Et::MakeStatusTrait>>
    : public std::true_type {};
template <typename _Et>
inline constexpr bool has_make_status_trait_v =
    has_make_status_trait<_Et>::value;

template <typename, typename = void>
struct has_base_error_type : public std::false_type {};
template <typename _Et>
struct has_base_error_type<_Et, std::void_t<typename _Et::BaseErrorType>>
    : public std::true_type {};
template <typename _Et>
inline constexpr bool has_base_error_type_v = has_base_error_type<_Et>::value;

// A tag-struct to provide into |Wrap| to explicitly discard the previous
// stack  after |WrapTransform|. That allows calling |Wrap| on a chain with
// different |BaseErrorType|.
struct WrapTransformOnly {};

// |StackableError| provides a unique_ptr-like access style for a stack of
// errors. It can be constructed from a raw pointer - to take an ownership of
// the pointer's lifetime - and linked with another StackableError via
// Wrap/Unwrap calls. The object implements iterator's traits to be used with
// range-for loops, and implements a |ToFullString| short-cut to combine the
// error messages of the whole stack.
// Since the object has unique_ptr-like semantics, it can never be copied, only
// moved, raw-ptr constructed or constructed from releasing another
// |StackableError|. The content of releasing the |StackableError| is an
// implementation detail and should always be referred as |auto&&| type. Storing
// a copy of the released value may succeed, but is treated as unspecified
// behaviour that is a subject to change at any time without prior notice.
//
// Despite being a unique_ptr representation, raw pointer access is a temporary
// convenience for converting existing code and will be deprecated, once the
// codebase adopts |ok()| checks.
// TODO(dlunev): Remove get/operator-> semantics when |ok()| is adopted.
//
// Template parameters meaning:
// _Et - head's error type. Must be derived from |Base|. Defaulter to |Base|
// _Rt - alias for |_Et| to trigger SFINAE in sub-templates.
// _Ut - uptype of |_Et| Type
// _Dt - a dynamic castable from |_Et| type
template <typename _Et = Error>
class StackableError {
 public:
  static_assert(has_make_status_trait_v<_Et>,
                "|_Et| type doesn't define |MakeStatusTrait|");
  static_assert(has_base_error_type_v<_Et>,
                "|_Et| type doesn't define |BaseErrorType|");

 private:
  // The use of |BaseErrorType| is a tranistional stop gap. Eventually the
  // type stored in stack should be equivalent to |_Et|. That may cause a
  // problem with upcasting the whole stack, since it would be expensive to
  // poke each unique pointer, thus some alternative storage arrangement
  // might be required.
  using stack_holder = StackHolderType<typename _Et::BaseErrorType>;

 public:
  // Mimic unique_ptr type aliases. Through out the code when we check nullness
  // of the head element to |pointer()|, not to the |nullptr|. While this
  // doesn't make any difference at the moment, it may become prominent if we
  // introduce support for the deleters. Deleters introduce the whole another
  // layer of complexity around internal pointer type deduction, to the point
  // where the internal pointer may no longer be a raw-pointer object - it can
  // be anything, up to and including an object with no comparison against
  // |nullptr| or the one that loses some information when compare to |nullptr|.
  // Thus, to safe-guard ourselves against it, we adopt a standard library style
  // of comparing the internal pointer of unique_ptr-like objects.
  using pointer = typename PointerHolderType<_Et>::pointer;
  using element_type = typename PointerHolderType<_Et>::element_type;
  // We do not support deleters presently, but plumb the deleter type through
  // to complete the object trait.
  using deleter_type = typename PointerHolderType<_Et>::deleter_type;
  // Export the types which are actually stored within the stack parallel to
  // |element_type| and |pointer|.
  using base_element_type = typename _Et::BaseErrorType;
  using base_pointer =
      typename StackPointerHolderType<base_element_type>::pointer;

  // Type aliases for ranges and iterators. The underlying types are the
  // implementation details and must not be relied upon directly.
  using iterator = StackableErrorIterator<base_element_type>;
  using const_iterator = StackableErrorConstIterator<base_element_type>;
  // Iterators can be obtained from range with |begin()| and |end()|
  using iterator_range = StackableErrorRange<base_element_type>;
  using const_iterator_range = StackableErrorConstRange<base_element_type>;

  static_assert(std::is_base_of_v<Error, base_element_type> ||
                    std::is_same_v<Error, base_element_type>,
                "|_Et::BaseErrorType| is not derived from |Error|");
  static_assert(std::is_base_of_v<base_element_type, _Et> ||
                    std::is_same_v<base_element_type, _Et>,
                "|_Et| is not derived from |_Et::BaseErrorType|");

 private:
  // Implementation details of the internal holder's types so the rest of the
  // code can be generic. The types and members in the section are considered
  // a StackableError backend. The idea of the split to prevent change of the
  // backend affecting the frontend code - frontend code should never touch
  // the object that represents backend directly.

  // Backend object. Currently represented as
  // std::list<std::unique_ptr<base_element_type>> (see
  // stackable_error_forward_declarations.h).
  //
  // Invariants:
  // * |error_stack_.front()| represents head of the stack.
  // * |error_stack_.front() + 1, error_stack_.end()| represents wrapped stack.
  // * |error_stack_.empty() == true| represents an OK chain.
  // * |stack_error_| never contains null objects.
  // * |stack_error_| stores error cast to the |_Et::BaseErrorType|.
  // * |stack_error_.front()| is castable to |_Et|. Combined with above, it
  //   ensures type information for the head object only. It is useful when
  //   there is no type conversion of returned value and the caller can directly
  //   check the head's properties without casting.
  stack_holder error_stack_;

  // Backend interface.

  // Resets the stack.
  void ResetInternal(pointer ptr) {
    error_stack_.clear();
    if (ptr != pointer()) {
      error_stack_.emplace_back(std::move(ptr));
    }
  }

  // Swaps the stacks of two chains.
  void SwapInternal(StackableError& other) noexcept {
    std::swap(error_stack_, other.error_stack_);
  }

  // Return true if the object represents an |ok()| sequence.
  bool IsOkInternal() const noexcept { return error_stack_.empty(); }

  // Returns the pointer to the head error object or |nullptr| representing
  // value.
  // TODO(dlunev): deprecate when codebase adopts |ok()| checks.
  pointer GetInternal() const noexcept {
    // Return a |nullptr| representing value for an OK status.
    if (IsOkInternal()) {
      return pointer();
    }

    // It is an invariant that the type of the head can cast to |pointer|. This
    // cast allows us to keep a uniformly typed internal stack across
    // polimorphic type specializations of the |StackableError| container -
    // since type casting a "list"-like container of "unique_ptr"-like objects
    // is expensive. We check the invariant in the debug builds.
    DCHECK_NE(error_stack_.front().get(), pointer())
        << " |nullptr| in error stack";
    DCHECK_NE(dynamic_cast<pointer>(error_stack_.front().get()), pointer())
        << " Head pointer is not of the expected type.";
    return static_cast<pointer>(error_stack_.front().get());
  }

  // Make current error to wrap another stack.
  void WrapInternal(stack_holder&& error_stack) noexcept {
    DCHECK(!error_stack_.empty())
        << " |WrapInternal| is called without checking |!ok()| of the wrapping "
        << "object";
    DCHECK(!error_stack.empty())
        << " |WrapInternal| is called without checking |!ok()| of the wrapped "
        << "object";
    DCHECK(error_stack_.size() == 1)
        << " |WrapInternal| is called without checking |IsWrappiing()| of the "
        << "wrapping object.";
    // The wrapped stack starts at |error_stack_.front() + 1|. It has to be
    // equal to |end()|, and the checks above ensure that.
    error_stack_.splice(error_stack_.end(), std::move(error_stack));
  }

  // Pop an error from the stack.
  void UnwrapInternal() { error_stack_.pop_front(); }

  // Check if the object already wraps a stack.
  bool IsWrappingInternal() const noexcept { return error_stack_.size() > 1; }

  // Returns a range object to use with range-for loops. Ensures const access
  // to the underlying object.
  const_iterator_range RangeInternal() const noexcept {
    return StackableErrorConstRangeFactory<base_element_type>()(
        error_stack_.begin(), error_stack_.end());
  }

  // Returns a range object to use with range-for loops. Allows non-const access
  // to the underlying object.
  iterator_range RangeInternal() noexcept {
    return StackableErrorRangeFactory<base_element_type>()(error_stack_.begin(),
                                                           error_stack_.end());
  }

 public:
  // The following code is considered StackablePointer's frontend. Constructors,
  // assign operators and release_stack are allowed to construct and move the
  // backend object, but they should not introspect into them. Other methods can
  // only use the backend interface methods.

  // Default constructor creates an empty stack to represent success.
  constexpr StackableError() noexcept : error_stack_() {}

  // |nullptr_t| constructor creates an empty stack to represent success.
  // We need an implicit conversion from |nullptr| to preserve the semantics of
  // the existing code, where returning |nullptr| represents success.
  // TODO(dlunev): disable implicit |nullptr| conversion when the codebase
  // adopts |OkStatus<T>|.
  constexpr StackableError(nullptr_t) noexcept  // NOLINT smart ptr nullptr
      : error_stack_() {}

  // Constructor from a raw pointer takes ownership of the pointer and puts it
  // on top of the stack.
  explicit StackableError(pointer ptr) : error_stack_() {
    ResetInternal(std::move(ptr));
  }

  // Constructor from the internal stack representation. Internal stack
  // representation must never be constructed directly and must only be
  // obtained through |release_stack()| method. Since the internal object's
  // representation doesn't posses type information, the constructor can only
  // be invoked the head type information is not present for the object, i.e.
  // |_Et| is |Base|. We disable the constructor with SFINAE.
  // We need to introduce a temporary argument |_Rt| in order to trigger SFINAE
  // on the template - it is only triggered for unresolved until now arguments,
  // and  thus |std::enable_if_t<std::is_same_v<base_element_type, _Et>>|
  // wouldn't work correctly. Since |_Rt| is not deducible from the context,
  // place it behind |ExplicitArgumentBarrier| idiom to disallow manual
  // specialization to bypass SFINAE. Manually specialized call can break the
  // invariant of the the head being castable to the class template argument
  // |_Et|.
  template <int&... ExplicitArgumentBarrier,
            typename _Rt = _Et,
            typename = std::enable_if_t<std::is_same_v<base_element_type, _Rt>>>
  explicit StackableError(stack_holder&& error_stack)
      : error_stack_(std::move(error_stack)) {}

  // Move constructor. Releases the backend object of |other| into our
  // backend object.
  StackableError(StackableError&& other)
      : error_stack_(other.release_stack()) {}

  // Converting move constructor from a compatible type. It is fine, since
  // our internal stack representation is of a base |Base| type anyway.
  // SFINAE checks that the supplied pointer type is compatible with this
  // object's head type. Since manually specializing the operator could lead to
  // breaking invariant of the head object being castable to class template type
  // |_Et|, we use |ExplicitArgumentBarrier| idiom to make |_Ut| auto-deducible
  // only.
  template <int&... ExplicitArgumentBarrier,
            typename _Ut,
            typename = std::enable_if_t<std::is_convertible_v<_Ut*, pointer>>>
  StackableError(StackableError<_Ut>&& other)
      : error_stack_(other.release_stack()) {
    static_assert(
        std::is_same_v<base_element_type, typename _Ut::BaseErrorType>,
        "|BaseErrorType| of |other| must be the same with |this|.");
  }

  // Move-assign operator. Releases the backend object of |other| into our
  // backend object.
  StackableError& operator=(StackableError&& other) {
    error_stack_ = other.release_stack();
    return *this;
  }

  // Converting move-assign operator from a compatible type. See the comments
  // on converting constructor for the template arguments explanation.
  template <int&... ExplicitArgumentBarrier,
            typename _Ut,
            typename = std::enable_if_t<std::is_convertible_v<_Ut*, pointer>>>
  StackableError& operator=(StackableError<_Ut>&& other) {
    static_assert(
        std::is_same_v<base_element_type, typename _Ut::BaseErrorType>,
        "|BaseErrorType| of |other| must be the same with |this|.");
    error_stack_ = other.release_stack();
    return *this;
  }

  // Disallow copy since we provide unique_ptr-like semantics.
  StackableError(const StackableError&) = delete;
  StackableError& operator=(const StackableError&) = delete;

  // Releases the content of the |StackableError| to be moved to another
  // |StackableError|. The returned value must always be referred as |auto&&|
  // type. Preserving a copy of the value may succeed, but is treated as an
  // unspecified behaviour and is subject to change at any time without prior
  // notice. The return object loses type information and only can be supplied
  // to |StackableError<base_element_type>| specialization.
  stack_holder&& release_stack() { return std::move(error_stack_); }

  // Returns true if StackableError represents a success.
  bool ok() const noexcept { return IsOkInternal(); }

  // Explicit conversion to bool returns true if the object doesn't represent
  // a a success. This semantic added to preserve the behaviour of the code upon
  // conversion, without the need to change every conditional. Current code
  // uses unique_ptr bool conversion semantic to detect a presence of an error.
  // TODO(dlunev): remove this once the code base is converted to use explicit
  // |!ok()| check.
  explicit operator bool() const noexcept { return !ok(); }

  // Const reference to the top error object. It is a logic error to query
  // |error()| or dereference a |StackableError| that represents success.
  // |noexcept(noexcept(*std::declval<pointer>()))| means that the function is
  // not throwing if dereferencing of the object of the pointer type would not
  // be throwing. Outer |noexcept| in the sequence is a function definition
  // keyword, which, when its argument is |true|, defines the function as
  // non-throwing. The inner |noexcept| is an operator that evaluates to |true|
  // if the operation supplied as its argument can throw. Combining those
  // together
  // * If |*std::declval<pointer>()| can throw - the inner |noexcept| will
  // return
  //   |true|, and |false| otherwise.
  // * The outer |noexcept| will consume the value, and based on it will declare
  //   the function as either throwing or non-throwing.
  std::add_lvalue_reference_t<element_type> error() const
      noexcept(noexcept(*std::declval<pointer>())) {
    CHECK(!ok()) << " Dereferencing an OK chain is not allowed";
    return *GetInternal();
  }

  // Dereferencing the stack is equivalent to calling |error()| method on it -
  // it returns const reference to the top error object.
  // See the explanation for |noexcept(noexcept(...))| in |error()| method
  // comment.
  std::add_lvalue_reference_t<element_type> operator*() const
      noexcept(noexcept(*std::declval<pointer>())) {
    return error();
  }

  // Returns the pointer to the head of the error stack or the value
  // representing a nullptr pointer.
  // TODO(dlunev): deprecate when codebase adopts |ok()| checks.
  pointer get() const noexcept { return GetInternal(); }

  // Returns the pointer to the head of the error stack or the value
  // representing a nullptr pointer.
  // TODO(dlunev): deprecate when codebase adopts |ok()| checks.
  pointer operator->() const noexcept {
    CHECK(!ok()) << " Arrow operator on an OK chain is not allowed";
    return get();
  }

  // Resets current stack as ok status.
  void reset(nullptr_t) noexcept { ResetInternal(pointer()); }

  // Resets current stack with a new error or ok status.
  void reset(pointer ptr = pointer()) { ResetInternal(std::move(ptr)); }

  // Swaps two stacks.
  void swap(StackableError& other) noexcept { SwapInternal(other); }

  // Returns range object for range-for loops. Ensures const access to the
  // underlying pointers data.
  const_iterator_range const_range() const noexcept { return RangeInternal(); }

  // Returns range object for range-for loops. Ensures const access to the
  // underlying pointers data.
  const_iterator_range range() const noexcept { return RangeInternal(); }

  // Returns range object for range-for loops. Allows non-const access to the
  // underlying pointers data.
  iterator_range range() noexcept { return RangeInternal(); }

  // Walks the stack of objects and combines the error messages of each object
  // on the stack.
  std::string ToFullString() const noexcept {
    if (ok()) {
      return "OK";
    }

    std::string result;
    for (auto error_obj_ptr : const_range()) {
      // Use |decltype| to deduce the correct type of the pointer for the
      // typesafe comparison. See explanation for the typesafe comparison
      // at |pointer| alias declaration.
      DCHECK_NE(error_obj_ptr, decltype(error_obj_ptr)())
          << " |nullptr| in the chain";
      if (!result.empty()) {
        result += ": ";
      }
      result += error_obj_ptr->ToString();
    }
    return result;
  }

  // Returns |true| if the object is wrapping another stack.
  // Returns |false| if the object is a stand alone error or is ok() object.
  bool IsWrapping() const noexcept { return IsWrappingInternal(); }

  // Make current error to wrap another stack. Do it in place without moving
  // ourselves out. Doesn't return a value, because it is not allowed to wrap
  // more than once.
  // It wouldn't break anything if the client would manually specialize the
  // template, for practically the tail would be cast to |Base| anyway, but
  // add |ExplicitArgumentBarrier| just for the safety of mind to make |_Ut|
  // automatically deducible only.
  template <int&... ExplicitArgumentBarrier, typename _Ut>
  void WrapInPlace(StackableError<_Ut>&& other) {
    static_assert(
        std::is_same_v<base_element_type, typename _Ut::BaseErrorType>,
        "|BaseErrorType| of |other| must be the same with |this|. "
        "Use |WrapTransformOnly| tag to drop previous stack.");
    CHECK(!other.ok()) << " Can't wrap an OK object.";
    CHECK(!ok()) << " OK object can't be wrapping.";
    CHECK(!IsWrapping()) << " Object can wrap only once.";

    // Call into current error's |WrapTransform| and provide it the range object
    // for the stack being wrapped. We do it before actual wrapping so the
    // current error doees not appear in the view. We provide const_view to
    // prevent the modification of previously stacked objects from transform to
    // disallow creating side effects on the stack.
    get()->WrapTransform(other.const_range());
    WrapInternal(other.release_stack());
  }

  // This is an overload of |WrapInPlace| that drops the previous stack. In that
  // case the code relies on a |WrapTransform| overload provided for the
  // |other|'s |BaseErrorType| to extract necessary info from the previous
  // stack.
  template <int&... ExplicitArgumentBarrier, typename _Ut>
  void WrapInPlace(StackableError<_Ut>&& other, WrapTransformOnly tag) {
    CHECK(!other.ok()) << " Can't wrap an OK object.";
    CHECK(!ok()) << " OK object can't be wrapping.";
    CHECK(!IsWrapping()) << " Object can wrap only once.";

    // Call into current error's |WrapTransform| and provide it the range object
    // for the stack being wrapped. We do it before actual wrapping so the
    // current error doees not appear in the view. We provide const_view to
    // prevent the modification of previously stacked objects from transform to
    // disallow creating side effects on the stack.
    get()->WrapTransform(other.const_range());

    // Discard the prior stack.
    other.reset();
  }

  // Make current error to wrap another stack. See template arguments
  // explanation in |WrapInPlace| comments.
  template <int&... ExplicitArgumentBarrier, typename _Ut>
  [[nodiscard]] auto&& Wrap(StackableError<_Ut>&& other) && {
    WrapInPlace(std::move(other));
    return std::move(*this);
  }

  // This is an overload of |Wrap| that drops the previous stack. In that case
  // the code relies on a |WrapTransform| overload provided for the |other|'s
  // |BaseErrorType| to extract necessary info from the previous stack.
  template <int&... ExplicitArgumentBarrier, typename _Ut>
  [[nodiscard]] auto&& Wrap(StackableError<_Ut>&& other,
                            WrapTransformOnly tag) && {
    WrapInPlace(std::move(other), tag);
    return std::move(*this);
  }

  // Pop an error from the stack. In-place unwrapping is allowed only for the
  // stack with |Base| set as the head type, because otherwise we need to
  // recreate the object with |Base| template argument.
  // See the explanation for the template arguments in converting
  // ctor/assignment operator.
  template <int&... ExplicitArgumentBarrier,
            typename _Rt = _Et,
            typename = std::enable_if_t<std::is_same_v<base_element_type, _Rt>>>
  auto& UnwrapInPlace() {
    CHECK(!ok()) << " OK object can't be unwrapped.";
    UnwrapInternal();
    return *this;
  }

  // Pop an error from the stack. rvalue return type is possible only in the
  // case where we already have |Base| as the class template argument |_Et|,
  // since otherwise we need to construct a new object via type conversion
  // constructor.
  // See the explanation for the template arguments in converting
  // ctor/assignment operator.
  template <int&... ExplicitArgumentBarrier,
            typename _Rt = _Et,
            typename = std::enable_if_t<std::is_same_v<base_element_type, _Rt>>>
  [[nodiscard]] StackableError<base_element_type>&& Unwrap() && {
    CHECK(!ok()) << " OK object can't be unwrapped.";
    UnwrapInternal();
    return std::move(*this);
  }

  // Pop an error from the stack. Since this will need to construct a new
  // object - return by copy-elided value instead of rvalue.
  // Note, that in this case we verify that |_Et| IS NOT |Base|.
  // See the explanation for the template arguments in converting
  // ctor/assignment operator.
  template <
      int&... ExplicitArgumentBarrier,
      typename _Rt = _Et,
      typename = std::enable_if_t<!std::is_same_v<base_element_type, _Rt>>>
  [[nodiscard]] StackableError<base_element_type> Unwrap() && {
    CHECK(!ok()) << " OK object can't be unwrapped.";
    UnwrapInternal();
    return std::move(*this);
  }

  // Check if the head was created as a down type.
  template <typename _Dt>
  bool Is() const noexcept {
    static_assert(std::is_base_of_v<base_element_type, _Dt> ||
                      std::is_same_v<base_element_type, _Dt>,
                  "Supplied type is not derived from the |Base| type.");
    return RTTI::Is<_Dt>(get());
  }

  // Returns head as a down type. The validity of the operation MUST be checked
  // by the prior call to Is<_Dt>.
  template <typename _Dt>
  _Dt* Cast() noexcept {
    static_assert(std::is_base_of_v<base_element_type, _Dt> ||
                      std::is_same_v<base_element_type, _Dt>,
                  "Supplied type is not derived from the |Base| type.");
    return RTTI::Cast<_Dt>(get());
  }

  // Const overload for |Cast<_Dt>| operation.
  template <typename _Dt>
  const _Dt* Cast() const noexcept {
    static_assert(std::is_base_of_v<base_element_type, _Dt> ||
                      std::is_same_v<base_element_type, _Dt>,
                  "Supplied type is not derived from the |Base| type.");
    return RTTI::Cast<_Dt>(get());
  }

  // Find returns a pointer to the first object of the specified kind in the
  // stack.
  template <typename _Dt>
  _Dt* Find() noexcept {
    static_assert(std::is_base_of_v<base_element_type, _Dt> ||
                      std::is_same_v<base_element_type, _Dt>,
                  "Supplied type is not derived from the |Base| type.");
    for (auto error_obj_ptr : range()) {
      // Use |decltype| to deduce the correct type of the pointer for the
      // typesafe comparison. See explanation for the typesafe comparison
      // at |pointer| alias declaration.
      DCHECK_NE(error_obj_ptr, decltype(error_obj_ptr)())
          << " |nullptr| in the chain";
      if (RTTI::Is<_Dt>(error_obj_ptr)) {
        return RTTI::Cast<_Dt>(error_obj_ptr);
      }
    }
    return nullptr;
  }

  // Const overload for |Find<_Dt>| operation.
  template <typename _Dt>
  const _Dt* Find() const noexcept {
    static_assert(std::is_base_of_v<base_element_type, _Dt> ||
                      std::is_same_v<base_element_type, _Dt>,
                  "Supplied type is not derived from the |Base| type.");
    for (const auto error_obj_ptr : const_range()) {
      // Use |decltype| to deduce the correct type of the pointer for the
      // typesafe comparison. See explanation for the typesafe comparison
      // at |pointer| alias declaration.
      DCHECK_NE(error_obj_ptr, decltype(error_obj_ptr)())
          << " |nullptr| in the chain";
      if (RTTI::Is<_Dt>(error_obj_ptr)) {
        return RTTI::Cast<_Dt>(error_obj_ptr);
      }
    }
    return nullptr;
  }
};

// Make |StackableError| printable.
template <typename _Et>
std::ostream& operator<<(std::ostream& os, const StackableError<_Et>& error) {
  os << error.ToFullString();
  return os;
}

// StackableError is only comparable to nullptr. This is required to preserve
// the behaviour of the current code, where comparing to nullptr may be used as
// a mean to check for success/failure.
// TODO(dlunev): remove this once the code base is converted to use |ok|
// instead of implicit nullptr/bool checks.

template <typename _Et>
inline bool operator==(const StackableError<_Et>& error, nullptr_t) {
  return error.get() == typename StackableError<_Et>::pointer();
}

template <typename _Et>
inline bool operator!=(const StackableError<_Et>& error, nullptr_t) {
  return error.get() != typename StackableError<_Et>::pointer();
}

template <typename _Et>
inline bool operator==(nullptr_t, const StackableError<_Et>& error) {
  return error.get() == typename StackableError<_Et>::pointer();
}

template <typename _Et>
inline bool operator!=(nullptr_t, const StackableError<_Et>& error) {
  return error.get() != typename StackableError<_Et>::pointer();
}

}  // namespace __impl
}  // namespace status
}  // namespace hwsec_foundation

// Make |StackableError| swappable.
namespace std {
template <typename _Et>
inline void swap(
    hwsec_foundation::status::__impl::StackableError<_Et>& s1,
    hwsec_foundation::status::__impl::StackableError<_Et>& s2) noexcept {
  s1.swap(s2);
}
}  // namespace std

#endif  // LIBHWSEC_FOUNDATION_STATUS_IMPL_STACKABLE_ERROR_H_
