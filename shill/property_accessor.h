// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_PROPERTY_ACCESSOR_
#define SHILL_PROPERTY_ACCESSOR_

#include <base/basictypes.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST.

#include "shill/accessor_interface.h"
#include "shill/error.h"
#include "shill/logging.h"

namespace shill {

// Templated implementations of AccessorInterface<>.
//
// PropertyAccessor<>, ConstPropertyAccessor<>, and
// WriteOnlyPropertyAccessor<> provide R/W, R/O, and W/O access
// (respectively) to the value pointed to by |property|.
//
// This allows a class to easily map strings to member variables, so that
// pieces of state stored in the class can be queried or updated by name.
//
//   bool foo = true;
//   map<string, BoolAccessor> accessors;
//   accessors["foo"] = BoolAccessor(new PropertyAccessor<bool>(&foo));
//   bool new_foo = accessors["foo"]->Get();  // new_foo == true
//   accessors["foo"]->Set(false);  // returns true, because setting is allowed.
//                                  // foo == false, new_foo == true
//   new_foo = accessors["foo"]->Get();  // new_foo == false
//   // Clear resets |foo| to its value when the PropertyAccessor was created.
//   accessors["foo"]->Clear();  // foo == true
//
// Generic accessors that provide write capability will check that the
// new value differs from the present one. If the old and new values
// are the same, the setter will not invoke the assignment operator, and
// will return false.
//
// Custom accessors are responsible for handling set-to-same-value
// themselves. It is not possible to handle that here, because some
// custom getters return default values, rather than the actual
// value. (I'm looking at you, WiFi::GetBgscanMethod.)
template <class T>
class PropertyAccessor : public AccessorInterface<T> {
 public:
  explicit PropertyAccessor(T *property)
      : property_(property), default_value_(*property) {
    DCHECK(property);
  }
  virtual ~PropertyAccessor() {}

  void Clear(Error *error) { Set(default_value_, error); }
  T Get(Error */*error*/) { return *property_; }
  bool Set(const T &value, Error */*error*/) {
    if (*property_ == value) {
      return false;
    }
    *property_ = value;
    return true;
  }

 private:
  T * const property_;
  const T default_value_;
  DISALLOW_COPY_AND_ASSIGN(PropertyAccessor);
};

template <class T>
class ConstPropertyAccessor : public AccessorInterface<T> {
 public:
  explicit ConstPropertyAccessor(const T *property) : property_(property) {
    DCHECK(property);
  }
  virtual ~ConstPropertyAccessor() {}

  void Clear(Error *error) {
    // TODO(quiche): check if this is the right error.
    // (maybe Error::kInvalidProperty instead?)
    error->Populate(Error::kInvalidArguments, "Property is read-only");
  }
  T Get(Error */*error*/) { return *property_; }
  bool Set(const T &/*value*/, Error *error) {
    // TODO(quiche): check if this is the right error.
    // (maybe Error::kPermissionDenied instead?)
    error->Populate(Error::kInvalidArguments, "Property is read-only");
    return false;
  }

 private:
  const T * const property_;
  DISALLOW_COPY_AND_ASSIGN(ConstPropertyAccessor);
};

template <class T>
class WriteOnlyPropertyAccessor : public AccessorInterface<T> {
 public:
  explicit WriteOnlyPropertyAccessor(T *property)
      : property_(property), default_value_(*property) {
    DCHECK(property);
  }
  virtual ~WriteOnlyPropertyAccessor() {}

  void Clear(Error *error) { Set(default_value_, error); }
  T Get(Error *error) {
    error->Populate(Error::kPermissionDenied, "Property is write-only");
    return T();
  }
  bool Set(const T &value, Error */*error*/) {
    if (*property_ == value) {
      return false;
    }
    *property_ = value;
    return true;
  }

 private:
  FRIEND_TEST(PropertyAccessorTest, SignedIntCorrectness);
  FRIEND_TEST(PropertyAccessorTest, UnsignedIntCorrectness);
  FRIEND_TEST(PropertyAccessorTest, StringCorrectness);

  T * const property_;
  const T default_value_;
  DISALLOW_COPY_AND_ASSIGN(WriteOnlyPropertyAccessor);
};

// CustomAccessor<> allows custom getter and setter methods to be provided.
// Thus, if the state to be returned is to be derived on-demand, or if
// setting the property requires validation, we can still fit it into the
// AccessorInterface<> framework.
//
// If the property is write-only, use CustomWriteOnlyAccessor instead.
template<class C, class T>
class CustomAccessor : public AccessorInterface<T> {
 public:
  // |target| is the object on which to call the methods |getter| and |setter|
  // |setter| is allowed to be NULL, in which case we will simply reject
  // attempts to set via the accessor. |setter| should return true if the
  // value was changed, and false otherwise.
  // It is an error to pass NULL for either of the other two arguments.
  CustomAccessor(C *target,
                 T(C::*getter)(Error *error),
                 bool(C::*setter)(const T &value, Error *error))
      : target_(target),
        default_value_(),
        getter_(getter),
        setter_(setter) {
    DCHECK(target);
    DCHECK(getter);  // otherwise, use CustomWriteOnlyAccessor
    if (setter_) {
      Error e;
      default_value_ = Get(&e);
    }
  }
  virtual ~CustomAccessor() {}

  void Clear(Error *error) { Set(default_value_, error); }
  T Get(Error *error) {
    return (target_->*getter_)(error);
  }
  bool Set(const T &value, Error *error) {
    if (setter_) {
      return (target_->*setter_)(value, error);
    } else {
      error->Populate(Error::kInvalidArguments, "Property is read-only");
      return false;
    }
  }

 private:
  C *const target_;
  // |default_value_| is non-const because it can't be initialized in
  // the initializer list.
  T default_value_;
  T(C::*const getter_)(Error *error);
  bool(C::*const setter_)(const T &value, Error *error);
  DISALLOW_COPY_AND_ASSIGN(CustomAccessor);
};

// CustomWriteOnlyAccessor<> allows a custom writer method to be provided.
// Get returns an error automatically. Clear resets the value to a
// default value.
template<class C, class T>
class CustomWriteOnlyAccessor : public AccessorInterface<T> {
 public:
  // |target| is the object on which to call |setter| and |clearer|.
  //
  // |target| and |setter| must be non-NULL. |setter| should return true
  // if the value was changed, and false otherwise.
  //
  // Either |clearer| or |default_value|, but not both, must be non-NULL.
  // Whichever is non-NULL is used to clear the property.
  CustomWriteOnlyAccessor(C *target,
                          bool(C::*setter)(const T &value, Error *error),
                          void(C::*clearer)(Error *error),
                          const T *default_value)
      : target_(target),
        setter_(setter),
        clearer_(clearer),
        default_value_() {
    DCHECK(target);
    DCHECK(setter);
    DCHECK(clearer || default_value);
    DCHECK(!clearer || !default_value);
    if (default_value) {
      default_value_ = *default_value;
    }
  }
  virtual ~CustomWriteOnlyAccessor() {}

  void Clear(Error *error) {
    if (clearer_) {
      (target_->*clearer_)(error);
    } else {
      Set(default_value_, error);
    }
  }
  T Get(Error *error) {
    error->Populate(Error::kPermissionDenied, "Property is write-only");
    return T();
  }
  bool Set(const T &value, Error *error) {
    return (target_->*setter_)(value, error);
  }

 private:
  C *const target_;
  bool(C::*const setter_)(const T &value, Error *error);
  void(C::*const clearer_)(Error *error);
  // |default_value_| is non-const because it can't be initialized in
  // the initializer list.
  T default_value_;
  DISALLOW_COPY_AND_ASSIGN(CustomWriteOnlyAccessor);
};

// CustomMappedAccessor<> passes an argument to the getter and setter
// so that a generic method can be used, for example one that accesses the
// property in a map.
template<class C, class T, class A>
class CustomMappedAccessor : public AccessorInterface<T> {
 public:
  // |target| is the object on which to call the methods |getter| and |setter|.
  // |setter| is allowed to be NULL, in which case we will simply reject
  // attempts to set via the accessor. |setter| should return true if the
  // value was changed, and false otherwise.
  // |argument| is passed to the getter and setter methods to disambiguate
  // between different properties in |target|.
  // It is an error to pass NULL for any of |target|, |clearer| or |getter|.
  CustomMappedAccessor(C *target,
                       void(C::*clearer)(const A &argument, Error *error),
                       T(C::*getter)(const A &argument, Error *error),
                       bool(C::*setter)(const A &argument, const T &value,
                                        Error *error),
                       const A &argument)
      : target_(target),
        clearer_(clearer),
        getter_(getter),
        setter_(setter),
        argument_(argument) {
    DCHECK(clearer);
    DCHECK(target);
    DCHECK(getter);
  }
  virtual ~CustomMappedAccessor() {}

  void Clear(Error *error) {
    (target_->*clearer_)(argument_, error);
  }
  T Get(Error *error) {
    return (target_->*getter_)(argument_, error);
  }
  bool Set(const T &value, Error *error) {
    if (setter_) {
      return (target_->*setter_)(argument_, value, error);
    } else {
      error->Populate(Error::kInvalidArguments, "Property is read-only");
      return false;
    }
  }

 private:
  C *const target_;
  void(C::*const clearer_)(const A &argument, Error *error);
  T(C::*const getter_)(const A &argument, Error *error);
  bool(C::*const setter_)(const A &argument, const T &value, Error *error);
  A argument_;
  DISALLOW_COPY_AND_ASSIGN(CustomMappedAccessor);
};

}  // namespace shill

#endif  // SHILL_PROPERTY_ACCESSOR_
