// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_PROMPT_REWRITER_H_
#define ODML_MANTIS_PROMPT_REWRITER_H_

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// This header file contains the utility function for rewriting user prompts
// used in the Generative Filling feature. The function addresses limitations
// in how the Generative Filling model processes natural language, particularly
// with verbs and prepositions.
//
// Specifically, the following scenarios are handled:
//
// 1.  **Verb Recognition:** The model struggles with verbs. This utility
// rewrites
//     prompts that include verb-preposition combinations (e.g., "replace the
//     cat with a dog") to include only the noun/adjective part (e.g., "a dog").
//
// 2.  **Removal Semantics:** Prompts with verbs that imply removal (e.g.,
// "remove the cat") trigger
//     rejection from the Trust and Safety service. These prompts are rewritten
//     to be empty strings, which are then treated as Reimaging cases.
//
// 3.  **Addition Semantics:** Prompts with addition verbs (e.g., "add a cat")
// are rewritten to
//     include only the noun part (e.g., "a cat").
//
// 4.  **Output:** The utility function processes user prompts to transform them
//     into a lowercase, noun/adjective or noun phrase and return the result.
//
// This is a workaround for the initial stage of the feature launch which
// focuses on English prompts. A more general approach to handling user prompts
// will be developed in the next phase.

namespace mantis {

// Rewrites a user prompt for better use with the Generative Fill feature.
std::string RewritePromptForGenerativeFill(const std::string& prompt);

}  // namespace mantis

#endif  // ODML_MANTIS_PROMPT_REWRITER_H_
