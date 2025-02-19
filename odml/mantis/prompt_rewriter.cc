// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/prompt_rewriter.h"

#include <iostream>
#include <string>

#include <base/containers/fixed_flat_set.h>
#include <base/no_destructor.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace mantis {
namespace {
// The input prompt is segmented by predefined separator characters, and
// matching is performed token by token.
constexpr const char kTokenDelimiter[] = ",.:;/'\"() ";

// The following types define the patterns for matching phrases like
// "put A on top of B".

// Represents a verb, which matches the first token of the input prompt.
using Verb = std::string;

// Represents a preposition phrase, which matches a sequence of tokens
// following the verb. For example, {"on", "top", "of"}.
using Phrase = std::vector<std::string>;

// Enum to specify which group of tokens to keep when a pattern is matched.
enum class GroupSelection {
  // Keep the tokens before the preposition phrase (e.g., "A" in "put A on top
  // of B").
  kKeepBeforeGroup,
  // Keep the tokens after the preposition phrase (e.g., "B" in "put A on top of
  // B").
  kKeepAfterGroup,
};

// Represents a pattern for matching a specific preposition phrase and
// the selection rule for the tokens. For example,
// {{"on", "top", "of"}, kKeepAfterGroup}
using PrepositionPattern = std::pair<Phrase, GroupSelection>;

// A constant lookup table that stores all supported verb and preposition
// phrase combinations. Each verb is associated with a vector of
// PrepositionPatterns.
//
// Example:
// {
//   {"put", { {{"on", "top", "of"}, kKeepAfterGroup}, {{"below"},
//   kKeepAfterGroup} }},
//   {"replace", { {{"with"}, kKeepAfterGroup} }},
//   {"add", { {{"to"}, kKeepBeforeGroup}, {{"next", "to"}, kKeepBeforeGroup} }}
// }
using VerbPrepositionMap = std::map<Verb, std::vector<PrepositionPattern>>;

const VerbPrepositionMap& GetVerbRewritePrepositionMap() {
  static const base::NoDestructor<VerbPrepositionMap> verb_preposition_map(
      VerbPrepositionMap{
          {"arrange",
           {
               {{"around"}, GroupSelection::kKeepBeforeGroup},
           }},
          {"change",
           {
               {{"for"}, GroupSelection::kKeepAfterGroup},
           }},
          {"convert",
           {
               {{"to"}, GroupSelection::kKeepAfterGroup},
           }},
          {"exchange",
           {
               {{"for"}, GroupSelection::kKeepAfterGroup},
           }},
          {"insert",
           {
               {{"between"}, GroupSelection::kKeepBeforeGroup},
               {{"in"}, GroupSelection::kKeepBeforeGroup},
               {{"into"}, GroupSelection::kKeepBeforeGroup},
           }},
          {"introduce",
           {
               {{"to"}, GroupSelection::kKeepBeforeGroup},
           }},
          {"morph",
           {
               {{"into"}, GroupSelection::kKeepAfterGroup},
           }},
          {"place",
           {
               {{"beneath"}, GroupSelection::kKeepBeforeGroup},
               {{"beside"}, GroupSelection::kKeepBeforeGroup},
               {{"in"}, GroupSelection::kKeepBeforeGroup},
               {{"on"}, GroupSelection::kKeepBeforeGroup},
               {{"under"}, GroupSelection::kKeepBeforeGroup},
           }},
          {"position",
           {
               {{"beside"}, GroupSelection::kKeepBeforeGroup},
               {{"next", "to"}, GroupSelection::kKeepBeforeGroup},
           }},
          {"put",
           {
               {{"at"}, GroupSelection::kKeepBeforeGroup},
               {{"in"}, GroupSelection::kKeepBeforeGroup},
               {{"near"}, GroupSelection::kKeepBeforeGroup},
               {{"on", "top", "of"}, GroupSelection::kKeepBeforeGroup},
               {{"on"}, GroupSelection::kKeepBeforeGroup},
           }},
          {"replace",
           {
               {{"with"}, GroupSelection::kKeepAfterGroup},
           }},
          {"substitute",
           {
               {{"for"}, GroupSelection::kKeepBeforeGroup},
           }},
          {"surround",
           {
               {{"with"}, GroupSelection::kKeepAfterGroup},
           }},
          {"swap",
           {
               {{"for"}, GroupSelection::kKeepAfterGroup},
               {{"with"}, GroupSelection::kKeepAfterGroup},
           }},
          {"switch",
           {
               {{"with"}, GroupSelection::kKeepAfterGroup},
           }},
          {"trade",
           {
               {{"for"}, GroupSelection::kKeepAfterGroup},
           }},
          {"transform",
           {
               {{"into"}, GroupSelection::kKeepAfterGroup},
           }},
          {"turn",
           {
               {{"into"}, GroupSelection::kKeepAfterGroup},
           }},
      });
  return *verb_preposition_map;
}

// Constant list of stopword verbs
constexpr auto kStopVerbs = base::MakeFixedFlatSet<std::string>({
    "remove",
    "erase",
    "eliminate",
    "cut",
    "disappear",
    "vanish",
    "wipe",
    "rid",
});

bool IsStopword(const std::string& token) {
  return kStopVerbs.count(token);
}

bool MatchPhrase(const std::vector<std::string>& tokens,
                 int index,
                 const Phrase& phrase) {
  for (int i = 0; i < phrase.size(); ++i) {
    if (i + index >= tokens.size()) {
      return false;
    }
    if (tokens[i + index] != phrase[i]) {
      return false;
    }
  }
  return true;
}

// Extracts tokens from a list based on the position of a preposition phrase.
// This function processes a vector of strings (tokens) and extracts a subset
// of tokens based on the presence of a specific preposition phrase.
//
// For example, It looks for the "put" verb and a preposition phrase like
// {"on", "top", "of"} and then keeps either tokens between those or after
// those tokens, as determined by a lookup table that maps verbs to
// preposition phrases and their associated GroupSelection rules.
std::vector<std::string> ExtractTokensByPreposition(
    const std::vector<std::string>& tokens) {
  const VerbPrepositionMap& verb_rewrite_preposition_map =
      GetVerbRewritePrepositionMap();
  for (int i = 0; i < tokens.size(); i++) {
    const auto matched_patterns = verb_rewrite_preposition_map.find(tokens[i]);
    if (matched_patterns == verb_rewrite_preposition_map.end()) {
      continue;
    }

    // skip at least one word and search for the preposition
    for (int j = i + 2; j < tokens.size(); ++j) {
      for (const auto& pattern : matched_patterns->second) {
        const auto& phrase = pattern.first;
        if (!MatchPhrase(tokens, j, phrase)) {
          continue;
        }

        // Found a match. Returns the specified token group.
        if (pattern.second == GroupSelection::kKeepBeforeGroup) {
          return std::vector<std::string>(tokens.begin() + i + 1,
                                          tokens.begin() + j);
        } else {
          return std::vector<std::string>(tokens.begin() + j + phrase.size(),
                                          tokens.end());
        }
      }
    }
  }
  return tokens;
}

const std::vector<Phrase>& GetAdditionVerbPhrases() {
  static const base::NoDestructor<std::vector<Phrase>> phrases({
      {"add", "in"},
      {"add"},
      {"affix"},
      {"attach"},
      {"create"},
      {"depict"},
      {"display"},
      {"drop"},
      {"embed"},
      {"fill", "with"},
      {"generate"},
      {"illustrate"},
      {"implant"},
      {"include"},
      {"insert"},
      {"make", "appear"},
      {"overlay", "with"},
      {"place", "onto"},
      {"place"},
      {"populate", "with"},
      {"portray"},
      {"position"},
      {"produce"},
      {"put", "into"},
      {"put"},
      {"render", "visible"},
      {"render"},
      {"set"},
      {"show"},
      {"superimpose"},
  });
  return *phrases;
}

// The function iterates through the `tokens` vector. For each token, it
// checks if it matches any of the addition semantic verbs defined in
// `kAdditionVerbs`. If a match is found, all the tokens that follow the
// verb are added to the returned vector.
//
// The function handles only the first occurrence of any of the specified
// verbs. If no match is found, original vector is returned.
// If an addition semantic verb is at the end of the input vector, an empty
// vector is returned.
std::vector<std::string> ExtractTokensAfterAdditionVerbs(
    const std::vector<std::string>& tokens) {
  for (int i = 0; i < tokens.size(); ++i) {
    for (const auto& phrase : GetAdditionVerbPhrases()) {
      if (MatchPhrase(tokens, i, phrase)) {
        return std::vector<std::string>(tokens.begin() + i + phrase.size(),
                                        tokens.end());
      }
    }
  }
  return tokens;
}

// Filters a vector of strings, returning an empty vector if a stopword is
// present.
std::vector<std::string> ClearIfStopwordPresent(
    const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (IsStopword(token)) {
      return {};
    }
  }
  return tokens;
}

}  // namespace

std::string RewritePromptForGenerativeFill(const std::string& prompt) {
  std::vector<std::string> tokens =
      base::SplitString(base::ToLowerASCII(prompt), kTokenDelimiter,
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  std::vector<std::string> preposition_extracted_tokens =
      ExtractTokensByPreposition(tokens);
  std::vector<std::string> addition_verb_extracted_tokens =
      ExtractTokensAfterAdditionVerbs(preposition_extracted_tokens);
  std::vector<std::string> final_tokens =
      ClearIfStopwordPresent(addition_verb_extracted_tokens);
  return base::JoinString(final_tokens, " ");
}

}  // namespace mantis
