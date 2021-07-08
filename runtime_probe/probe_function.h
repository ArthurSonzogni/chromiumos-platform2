// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_PROBE_FUNCTION_H_
#define RUNTIME_PROBE_PROBE_FUNCTION_H_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <base/json/json_writer.h>
#include <base/values.h>
#include <base/strings/string_util.h>

#include "runtime_probe/probe_function_argument.h"

namespace runtime_probe {

// Creates a probe function. This is a syntax suger for |FromKwargsValue|.
template <typename T>
constexpr auto CreateProbeFunction = &T::template FromKwargsValue<T>;

class ProbeFunction {
  // ProbeFunction is the base class for all probe functions.  A derived
  // class should implement required virtual functions and contain some static
  // members: |function_name|, FromKwargsValue().
  //
  // FromKwargsValue is the main point to create a probe function instance.  It
  // takes a dictionary value in type base::Value as arguments and returns a
  // pointer to the instance of the probe function.
  //
  // Formally, a probe function will be represented as following structure::
  //   {
  //     <function_name:string>: <args:ArgsType>
  //   }
  //
  // where the top layer dictionary should have one and only one key.  For
  // example::
  //   {
  //     "sysfs": {
  //       "dir_path": "/sys/class/cool/device/dev*",
  //       "keys": ["key_1", "key_2"],
  //       "optional_keys": ["opt_key_1"]
  //     }
  //   }
  //
  // TODO(stimim): implement the following syntax.
  //
  // Alternative Syntax::
  //   1. single string ("<function_name:string>"), this is equivalent to::
  //      {
  //        <function_name:string>: {}
  //      }
  //
  //   2. single string ("<function_name:string>:<arg:string>"), this is
  //      equivalent to::
  //      {
  //        <function_name:string>: {
  //          "__only_required_argument": {
  //            <arg:string>
  //          }
  //        }
  //      }

 public:
  using DataType = std::vector<base::Value>;

  // Returns the name of the probe function.  The returned value should always
  // identical to the static member |function_name| of the derived class.
  //
  // A common implementation can be declared by macro NAME_PROBE_FUNCTION(name)
  // below.
  virtual const std::string& GetFunctionName() const = 0;

  // Converts |dv| with function name as key to ProbeFunction.  Returns nullptr
  // on failure.
  static std::unique_ptr<ProbeFunction> FromValue(const base::Value& dv);

  // Creates a probe function of type |T| with empty argument.
  // This function can be overridden by the derived classes.  See
  // `functions/sysfs.h` about how to implement this function.
  template <typename T>
  static auto FromKwargsValue(const base::Value& dict_value) {
    PARSE_BEGIN();
    PARSE_END();
  }

  // Evaluates this probe function. Returns a list of base::Value. For the probe
  // function that requests sandboxing, see |PrivilegedProbeFunction|.
  virtual DataType Eval() const { return EvalImpl(); }

  // This is for helper to evaluate the probe function. Helper is designed for
  // portion that need extended sandbox. See |PrivilegedProbeFunction| for more
  // detials.
  //
  // Output will be an integer and the interpretation of the integer on
  // purposely leaves to the caller because it might execute other binary
  // in sandbox environment and we might want to preserve the exit code.
  int EvalInHelper(std::string* output) const;

  using FactoryFunctionType =
      std::function<std::unique_ptr<ProbeFunction>(const base::Value&)>;

  // Mapping from |function_name| to FromKwargsValue() of each derived classes.
  static std::map<std::string_view, FactoryFunctionType> registered_functions_;

  virtual ~ProbeFunction() = default;

 protected:
  ProbeFunction() = default;
  ProbeFunction(const ProbeFunction&) = delete;
  explicit ProbeFunction(base::Value&& raw_value);

 private:
  // Implement this method to provide the probing. The output should be a list
  // of base::Value.
  virtual DataType EvalImpl() const = 0;

  // Each probe function must define their own args type.
};

class PrivilegedProbeFunction : public ProbeFunction {
  // |PrivilegedProbeFunction| run in the sandbox with pre-defined permissions.
  // This is for all the operations which request special permission like sysfs
  // access. |PrivilegedProbeFunction| will be initialized with same json
  // statement in the helper process, which invokes |EvalImpl()|. Since
  // execution of |PrivilegedProbeFunction::EvalImpl()| implies a different
  // sandbox, it is encouraged to keep work that doesn't need a privilege in
  // |PostHelperEvalImpl()|.
  //
  // For each |PrivilegedProbeFunction|, please modify `sandbox/args.json` and
  // `sandbox/${ARCH}/${function_name}-seccomp.policy`.
 public:
  DataType Eval() const final;

  // Redefine this to access protected constructor.
  template <typename T>
  static auto FromKwargsValue(const base::Value& dict_value) {
    PARSE_BEGIN();
    PARSE_END();
  }

 protected:
  PrivilegedProbeFunction() = delete;
  PrivilegedProbeFunction(const PrivilegedProbeFunction&) = delete;
  explicit PrivilegedProbeFunction(base::Value&& raw_value);

  // Serializes this probe function and passes it to helper. The output of the
  // helper will store in |result|. Returns true if success on executing helper.
  bool InvokeHelper(std::string* result) const;

  // Serializes this probe function and passes it to helper.  Helper function
  // for InvokeHelper() where the output is known in advanced in JSON format.
  // The transform of JSON will be automatically applied.  Returns base::nullopt
  // on failure.
  base::Optional<base::Value> InvokeHelperToJSON() const;

 private:
  // This method is called after |EvalImpl()| finished. The |result| is the
  // value returned by |EvalImpl()|. Because |EvalImpl()| is executed in helper,
  // this method is for those operations that cannot or don't want to be
  // performed in helper, for example dbus call. This method can do some extra
  // logic out of helper and modify the |result|. See b/185292404 for the
  // discussion about this two steps EvalImpl.
  virtual void PostHelperEvalImpl(DataType* result) const {}

  // The value to describe this probe function.
  base::Value raw_value_;
};

#define NAME_PROBE_FUNCTION(name)                       \
  const std::string& GetFunctionName() const override { \
    static const std::string instance(function_name);   \
    return instance;                                    \
  }                                                     \
  static constexpr auto function_name = name

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_PROBE_FUNCTION_H_
