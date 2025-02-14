// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generic and provider-independent Variable subclasses. These variables can be
// used by any state provider to implement simple variables to avoid repeat the
// same common code on different state providers.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_GENERIC_VARIABLES_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_GENERIC_VARIABLES_H_

#include <string>

#include <base/functional/callback.h>

#include "update_engine/update_manager/variable.h"

namespace chromeos_update_manager {

// Variable class returning a copy of a given object using the copy constructor.
// This template class can be used to define variables that expose as a variable
// any fixed object, such as the a provider's private member. The variable will
// create copies of the provided object using the copy constructor of that
// class.
//
// For example, a state provider exposing a private member as a variable can
// implement this as follows:
//
//   class SomethingProvider {
//    public:
//      SomethingProvider(...) {
//        var_something_foo = new PollCopyVariable<MyType>(foo_);
//      }
//      ...
//    private:
//     MyType foo_;
//   };
template <typename T>
class PollCopyVariable : public Variable<T> {
 public:
  // Creates the variable returning copies of the passed |ref|. The reference to
  // this object is kept and it should be available whenever the GetValue()
  // method is called. If |is_set_p| is not null, then this flag will be
  // consulted prior to returning the value, and an |errmsg| will be returned if
  // it is not set.
  PollCopyVariable(const std::string& name,
                   const T& ref,
                   const bool* is_set_p,
                   const std::string& errmsg)
      : Variable<T>(name, kVariableModePoll),
        ref_(ref),
        is_set_p_(is_set_p),
        errmsg_(errmsg) {}
  PollCopyVariable(const std::string& name, const T& ref, const bool* is_set_p)
      : PollCopyVariable(name, ref, is_set_p, std::string()) {}
  PollCopyVariable(const std::string& name, const T& ref)
      : PollCopyVariable(name, ref, nullptr) {}

  PollCopyVariable(const std::string& name,
                   const base::TimeDelta poll_interval,
                   const T& ref,
                   const bool* is_set_p,
                   const std::string& errmsg)
      : Variable<T>(name, poll_interval),
        ref_(ref),
        is_set_p_(is_set_p),
        errmsg_(errmsg) {}
  PollCopyVariable(const std::string& name,
                   const base::TimeDelta poll_interval,
                   const T& ref,
                   const bool* is_set_p)
      : PollCopyVariable(name, poll_interval, ref, is_set_p, std::string()) {}
  PollCopyVariable(const std::string& name,
                   const base::TimeDelta poll_interval,
                   const T& ref)
      : PollCopyVariable(name, poll_interval, ref, nullptr) {}

 protected:
  FRIEND_TEST(UmPollCopyVariableTest, SimpleTest);
  FRIEND_TEST(UmPollCopyVariableTest, UseCopyConstructorTest);

  // Variable override.
  inline const T* GetValue(base::TimeDelta /* timeout */,
                           std::string* errmsg) override {
    if (is_set_p_ && !(*is_set_p_)) {
      if (errmsg) {
        if (errmsg_.empty()) {
          *errmsg = "No value set for " + this->GetName();
        } else {
          *errmsg = errmsg_;
        }
      }
      return nullptr;
    }
    return new T(ref_);
  }

 private:
  // Reference to the object to be copied by GetValue().
  const T& ref_;

  // A pointer to a flag indicating whether the value is set. If null, then the
  // value is assumed to be set.
  const bool* const is_set_p_;

  // An error message to be returned when attempting to get an unset value.
  const std::string errmsg_;
};

// Variable class returning a constant value that is cached on the variable when
// it is created.
template <typename T>
class ConstCopyVariable : public Variable<T> {
 public:
  // Creates the variable returning copies of the passed |obj|. The value passed
  // is copied in this variable, and new copies of it will be returned by
  // GetValue().
  ConstCopyVariable(const std::string& name, const T& obj)
      : Variable<T>(name, kVariableModeConst), obj_(obj) {}

 protected:
  // Variable override.
  const T* GetValue(base::TimeDelta /* timeout */,
                    std::string* /* errmsg */) override {
    return new T(obj_);
  }

 private:
  // Value to be copied by GetValue().
  const T obj_;
};

// Variable class returning a copy of a value returned by a given function. The
// function is called every time the variable is being polled.
template <typename T>
class CallCopyVariable : public Variable<T> {
 public:
  CallCopyVariable(const std::string& name,
                   base::RepeatingCallback<T(void)> func)
      : Variable<T>(name, kVariableModePoll), func_(func) {}
  CallCopyVariable(const std::string& name,
                   const base::TimeDelta poll_interval,
                   base::RepeatingCallback<T(void)> func)
      : Variable<T>(name, poll_interval), func_(func) {}
  CallCopyVariable(const CallCopyVariable&) = delete;
  CallCopyVariable& operator=(const CallCopyVariable&) = delete;

 protected:
  // Variable override.
  const T* GetValue(base::TimeDelta /* timeout */,
                    std::string* /* errmsg */) override {
    if (func_.is_null()) {
      return nullptr;
    }
    return new T(func_.Run());
  }

 private:
  FRIEND_TEST(UmCallCopyVariableTest, SimpleTest);

  // The function to be called, stored as a base::Callback.
  base::RepeatingCallback<T(void)> func_;
};

// A Variable class to implement simple Async variables. It provides two methods
// SetValue and UnsetValue to modify the current value of the variable and
// notify the registered observers whenever the value changed.
//
// The type T needs to be copy-constructible, default-constructible and have an
// operator== (to determine if the value changed), which makes this class
// suitable for basic types.
template <typename T>
class AsyncCopyVariable : public Variable<T> {
 public:
  explicit AsyncCopyVariable(const std::string& name)
      : Variable<T>(name, kVariableModeAsync), has_value_(false) {}

  AsyncCopyVariable(const std::string& name, const T value)
      : Variable<T>(name, kVariableModeAsync),
        has_value_(true),
        value_(value) {}

  void SetValue(const T& new_value) {
    bool should_notify = !(has_value_ && new_value == value_);
    value_ = new_value;
    has_value_ = true;
    if (should_notify) {
      this->NotifyValueChanged();
    }
  }

  void UnsetValue() {
    if (has_value_) {
      has_value_ = false;
      this->NotifyValueChanged();
    }
  }

 protected:
  // Variable override.
  const T* GetValue(base::TimeDelta /* timeout */,
                    std::string* errmsg) override {
    if (!has_value_) {
      if (errmsg) {
        *errmsg = "No value set for " + this->GetName();
      }
      return nullptr;
    }
    return new T(value_);
  }

 private:
  // Whether the variable has a value set.
  bool has_value_;

  // Copy of the object to be returned by GetValue().
  T value_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_GENERIC_VARIABLES_H_
