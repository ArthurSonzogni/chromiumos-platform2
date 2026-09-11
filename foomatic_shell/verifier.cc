// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "foomatic_shell/verifier.h"

#include <algorithm>
#include <array>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <base/no_destructor.h>

namespace foomatic_shell {

namespace {

// A set of allowed environment variables that may be set for executed commands.
const std::set<std::string>& AllowedVariables() {
  static const base::NoDestructor<std::set<std::string>> variables({"NOPDF"});
  return *variables;
}

bool HasPrefix(const std::string& str, const std::string_view& prefix) {
  if (prefix.size() > str.size()) {
    return false;
  }
  return (str.compare(0, prefix.size(), prefix) == 0);
}

bool HasSuffix(const std::string& str, const std::string_view& suffix) {
  if (suffix.size() > str.size()) {
    return false;
  }
  return (str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0);
}

// Return the first element with the given prefix or an empty string if no
// matching elements were found.
std::string FirstWithPrefix(const std::vector<std::string>& strs,
                            const std::string& prefix) {
  for (const auto& str : strs) {
    if (HasPrefix(str, prefix)) {
      return str;
    }
  }
  return "";
}

template <typename element>
bool Contains(const std::vector<element>& v, element e) {
  return std::find(v.begin(), v.end(), e) != v.end();
}

// Parse command line GNU parameters. Only parameters defined in the first four
// vectors passed to the function are allowed. All parsed parameters are added
// to the last two vectors passed to the function. Arguments of the parameters
// are skipped. The function returns true <=> all command line parameters were
// parsed successfully.
bool ParseGnuParameters(
    const std::vector<char>& shortParamsWithoutArg,
    const std::vector<char>& shortParamsWithArg,
    const std::vector<std::string>& longParamsWithoutArg,
    const std::vector<std::string>& longParamsWithArg,
    const std::vector<StringAtom>& parameters,
    std::vector<std::string>& outOptionParams,
    std::vector<std::string>& outNonOptionParams,
    const std::vector<char>& shortParamsWithOptionalArg = {},
    const std::vector<std::string>& longParamsWithOptionalArg = {}) {
  bool argument_expected = false;
  bool no_more_options = false;
  for (const auto& parameter : parameters) {
    const std::string& param = parameter.value;
    if (argument_expected) {
      // This string is an argument required by the previous parameter.
      argument_expected = false;
      continue;
    }
    // Non-option parameter: not starting with '-' or equals "-" or after "--".
    if (param.size() < 2 || param[0] != '-' || no_more_options) {
      outNonOptionParams.push_back(param);
      continue;
    }
    // Now, we have only parameters starting from '-' and having at least two
    // characters. First check if the parameter begins with '--'.
    if (param[1] == '-') {
      // "--" means no more option parameters.
      if (param.size() == 2) {  // == "--"
        no_more_options = true;
        continue;
      }
      // Check parameters starting from "--".
      auto equalCharPos = param.find('=');
      bool withArg = (equalCharPos != std::string::npos);
      std::string paramName;
      if (withArg) {
        paramName = param.substr(2, equalCharPos - 2);
      } else {
        paramName = param.substr(2);
      }
      auto matchingParam = FirstWithPrefix(longParamsWithoutArg, paramName);
      if (!matchingParam.empty()) {
        if (withArg) {
          // Unexpected argument.
          return false;
        }
        outOptionParams.push_back(std::string("--") + matchingParam);
        continue;
      }
      matchingParam = FirstWithPrefix(longParamsWithArg, paramName);
      if (!matchingParam.empty()) {
        argument_expected = !withArg;
        outOptionParams.push_back(std::string("--") + matchingParam);
        continue;
      }
      matchingParam = FirstWithPrefix(longParamsWithOptionalArg, paramName);
      if (!matchingParam.empty()) {
        outOptionParams.push_back(std::string("--") + matchingParam);
        continue;
      }
      // Unknown or banned parameter.
      return false;
    }
    // The parameter begins with single '-'. It may contain several options
    // glued together.
    for (size_t i = 1; i < param.size(); ++i) {
      if (Contains(shortParamsWithoutArg, param[i])) {
        // These parameters do not have an argument.
        outOptionParams.push_back(std::string("-") + param.substr(i, 1));
        continue;
      }
      if (Contains(shortParamsWithArg, param[i])) {
        // This option requires an argument. If it is the last character of
        // the parameter the argument is provided in the next parameter.
        // Otherwise, the remaining part of the parameter is the value.
        argument_expected = (i == param.size() - 1);
        outOptionParams.push_back(std::string("-") + param.substr(i, 1));
        break;
      }
      if (Contains(shortParamsWithOptionalArg, param[i])) {
        // This option may have an argument. In this case, the remaining part of
        // the parameter is the value.
        outOptionParams.push_back(std::string("-") + param.substr(i, 1));
        break;
      }
      // Unknown or banned parameter.
      return false;
    }
  }
  if (argument_expected) {
    // The last parameter has missing argument.
    return false;
  }
  return true;
}

}  // namespace

bool Verifier::VerifyScript(Script* script, int recursion_level) {
  DCHECK(script != nullptr);
  if (recursion_level > 5) {
    message_ = "too many recursive subshell invocations";
    return false;
  }

  for (auto& pipeline : script->pipelines) {
    for (auto& segment : pipeline.segments) {
      // Save the position of the current segment (in case of an error).
      position_ = Position(segment);
      // Verify the segment.
      bool result = false;
      if (segment.command) {
        // It is a Command.
        result = VerifyCommand(segment.command.get());
      } else {
        // It is a Script.
        DCHECK(segment.script);
        result = VerifyScript(segment.script.get(), recursion_level + 1);
      }
      if (!result) {
        return false;
      }
    }
  }
  return true;
}

bool Verifier::VerifyCommand(Command* command) {
  DCHECK(command != nullptr);

  // Verify variables set for this command.
  for (auto& var : command->variables_with_values) {
    if (AllowedVariables().count(var.variable.value) == 0) {
      message_ = "variable " + var.variable.value + " is not allowed";
      return false;
    }
    if (var.new_value.value.find('\0') != std::string::npos) {
      message_ = "variable " + var.variable.value + " has banned character";
      return false;
    }
  }

  const std::string& cmd = command->application.value;

  // Make sure that parameters do not contain C end-of-string character.
  for (auto& parameter : command->parameters) {
    if (parameter.value.find('\0') != std::string::npos) {
      message_ = "command " + cmd + " has parameter with banned character";
      return false;
    }
  }

  // The "cat" command is allowed <=> it has no parameters or it has only a
  // single parameter "-".
  if (cmd == "cat") {
    if (command->parameters.empty()) {
      return true;
    }
    if (command->parameters.size() == 1 &&
        command->parameters.front().value == "-") {
      return true;
    }
    message_ = "cat: disallowed parameter";
    return false;
  }

  // The "cut" command is verified in a separate method.
  if (cmd == "cut") {
    return VerifyCut(command->parameters);
  }

  // The "date" command is verified in a separate method.
  if (cmd == "date") {
    return VerifyDate(command->parameters);
  }

  // The "echo" command is always allowed.
  if (cmd == "echo") {
    return true;
  }

  // The "gs" command is verified in a separate method.
  if (cmd == "gs") {
    return VerifyGs(command->parameters);
  }

  // The "pdftops" command used by foomatic-rip is located at
  // /usr/libexec/cups/filter/pdftops, not /usr/bin/pdftops (a default one).
  // It takes 5 or 6 parameters.
  if (cmd == "pdftops") {
    if (command->parameters.size() < 5 || command->parameters.size() > 6) {
      message_ = "pdftops: wrong number of parameters";
      return false;
    }
    if (command->parameters.size() == 6) {
      const std::string& path = command->parameters[5].value;
      if (path == "-") {
        return true;
      }
      if (HasPrefix(path, "/var/spool/cups/") &&
          path.find("..") == std::string::npos) {
        return true;
      }
      message_ = "pdftops: disallowed filename";
      return false;
    }
    return true;
  }

  // The "printf" command is always allowed.
  if (cmd == "printf") {
    return true;
  }

  // The "sed" command is verified in a separate method.
  if (cmd == "sed") {
    if (!VerifySed(command->parameters)) {
      return false;
    }
    // The "--sandbox" parameter is added as the first parameter.
    StringAtom string_atom;
    string_atom.value = "--sandbox";
    string_atom.begin = string_atom.end = command->application.end;
    command->parameters.insert(command->parameters.begin(), string_atom);
    return true;
  }

  // All other commands are disallowed.
  message_ = "disallowed command: " + command->application.value;
  return false;
}

// Verify parameters for "cut" command. No input files different than '-' are
// allowed.
bool Verifier::VerifyCut(const std::vector<StringAtom>& parameters) {
  static const auto kLongParamsWithoutArg = std::vector<std::string>{
      "complement", "only-delimited", "zero-terminated"};
  static const auto kLongParamsWithArg = std::vector<std::string>{
      "bytes", "characters", "delimiter", "fields", "output-delimiter"};
  static const auto kShortParamsWithoutArg = std::vector{'n', 's', 'z'};
  static const auto kShortParamsWithArg = std::vector{'b', 'c', 'd', 'f'};
  std::vector<std::string> optionParams;
  std::vector<std::string> nonOptionParams;
  if (!ParseGnuParameters(kShortParamsWithoutArg, kShortParamsWithArg,
                          kLongParamsWithoutArg, kLongParamsWithArg, parameters,
                          optionParams, nonOptionParams)) {
    message_ = "cut: unknown or banned parameter";
    return false;
  }

  for (const auto& param : nonOptionParams) {
    if (param != "-") {
      message_ = "cut: file parameter not equals '-'";
      return false;
    }
  }

  return true;
}

// Verify parameters for "date" command. Forbidden parameters:
//   * -f, --file=DATEFILE
//   * -r, --reference=FILE
//   * -s, --set=STRING
// Only non-option parameters starting from '+' are allowed.
bool Verifier::VerifyDate(const std::vector<StringAtom>& parameters) {
  static const auto kLongParamsWithoutArg = std::vector<std::string>{
      "debug", "resolution", "rfc-email", "utc", "universal"};
  static const auto kLongParamsWithArg =
      std::vector<std::string>{"date", "rfc-3339"};
  static const auto kLongParamsWithOptionalArg =
      std::vector<std::string>{"iso-8601"};
  static const auto kShortParamsWithoutArg = std::vector{'R', 'u'};
  static const auto kShortParamsWithArg = std::vector{'d'};
  static const auto kShortParamsWithOptionalArg = std::vector{'I'};
  std::vector<std::string> optionParams;
  std::vector<std::string> nonOptionParams;
  if (!ParseGnuParameters(
          kShortParamsWithoutArg, kShortParamsWithArg, kLongParamsWithoutArg,
          kLongParamsWithArg, parameters, optionParams, nonOptionParams,
          kShortParamsWithOptionalArg, kLongParamsWithOptionalArg)) {
    message_ = "date: unknown or banned parameter";
    return false;
  }
  for (const auto& param : nonOptionParams) {
    if (!HasPrefix(param, "+")) {
      message_ = "date: disallowed positional parameter";
      return false;
    }
  }
  return true;
}

// Parameters “-dSAFER” and “-sOutputFile=-” must be present.
// No other “-sOutputFile=” parameters are allowed.
// All other parameters cannot start from prefixes defined in the array below.
bool Verifier::VerifyGs(const std::vector<StringAtom>& parameters) {
  static constexpr std::array<std::string_view, 11> kBannedPrefixes = {
      "--permit-file-",     "-I",        "-c", "-dALLOWPSTRANSPARENCY",
      "-dDELAYSAFER",       "-dNOSAFER", "-o", "-sOutputFile=",
      "-sOutputICCProfile", "-sstdout=", "@"};
  bool safer = false;
  bool output_file = false;
  for (auto& parameter : parameters) {
    const std::string& param = parameter.value;
    if (param == "-dPARANOIDSAFER" || param == "-dSAFER") {
      safer = true;
      continue;
    }
    if (param == "-sOutputFile=-" || param == "-sOutputFile=%stdout") {
      output_file = true;
      continue;
    }
    // Special case for @: we have to allow for *.upp files.
    if (HasPrefix(param, "@") && HasSuffix(param, ".upp") &&
        param.find('/') == std::string::npos) {
      continue;
    }
    for (auto& banned : kBannedPrefixes) {
      if (HasPrefix(param, banned)) {
        message_ = "gs: disallowed parameter";
        return false;
      }
    }
  }
  if (!safer) {
    message_ = "gs: the parameter -dSAFER is missing";
    return false;
  }
  if (!output_file) {
    message_ = "gs: the parameter -sOutputFile=- is missing";
    return false;
  }
  return true;
}

// Verify parameters for "sed" command. Forbidden parameters:
//   * -f script-file, --file=script-file
//   * -i[SUFFIX], --in-place[=SUFFIX]
// The only allowed input file is '-'.
bool Verifier::VerifySed(const std::vector<StringAtom>& parameters) {
  static const auto kLongParamsWithoutArg = std::vector<std::string>{
      "quiet",           "silent",   "debug",   "follow-symlinks", "posix",
      "regexp-extended", "separate", "sandbox", "unbuffered",      "null-data"};
  static const auto kLongParamsWithArg =
      std::vector<std::string>{"expression", "line-length"};
  static const auto kShortParamsWithoutArg =
      std::vector{'n', 'E', 'r', 's', 'u', 'z'};
  static const auto kShortParamsWithArg = std::vector{'e', 'l'};
  std::vector<std::string> optionParams;
  std::vector<std::string> nonOptionParams;
  if (!ParseGnuParameters(kShortParamsWithoutArg, kShortParamsWithArg,
                          kLongParamsWithoutArg, kLongParamsWithArg, parameters,
                          optionParams, nonOptionParams)) {
    message_ = "sed: unknown or banned parameter";
    return false;
  }

  // If no expressions have been provided, the first non-option parameter is an
  // expression.
  if (!nonOptionParams.empty() && !Contains<std::string>(optionParams, "-e") &&
      !Contains<std::string>(optionParams, "--expression")) {
    nonOptionParams.erase(nonOptionParams.begin());
  }

  for (const auto& param : nonOptionParams) {
    if (param != "-") {
      message_ = "sed: file parameter not equals '-'";
      return false;
    }
  }

  return true;
}

}  // namespace foomatic_shell
