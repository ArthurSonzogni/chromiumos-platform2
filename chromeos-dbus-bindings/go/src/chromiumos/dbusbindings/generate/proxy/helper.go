// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package proxy

import (
	"fmt"
	"strings"

	"chromiumos/dbusbindings/introspect"
)

// Returns stringified C++ type for signal callback.
func makeSignalCallbackType(args []introspect.SignalArg) (string, error) {
	if len(args) == 0 {
		return "base::RepeatingClosure", nil
	}

	var lines []string
	for _, a := range args {
		line, err := a.CallbackType()
		if err != nil {
			return "", err
		}
		lines = append(lines, line)
	}
	const (
		prefix = "const base::RepeatingCallback<void("
		suffix = ")>&"
	)
	indent := strings.Repeat(" ", len(prefix))
	return fmt.Sprintf("%s%s%s", prefix, strings.Join(lines, ",\n"+indent), suffix), nil
}
