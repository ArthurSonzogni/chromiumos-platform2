// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package proxy

import (
	"testing"

	"chromiumos/dbusbindings/introspect"
)

func TestMakeSignalCallbackType(t *testing.T) {
	cases := []struct {
		args []introspect.SignalArg
		want string
	}{{
		args: []introspect.SignalArg{},
		want: "base::RepeatingClosure",
	}, {
		args: []introspect.SignalArg{{
			Type: "ay",
		}},
		want: "const base::RepeatingCallback<void(const std::vector<uint8_t>&)>&",
	}, {
		args: []introspect.SignalArg{{
			Type: "i",
		}, {
			Type: "x",
		}, {
			Type: "(sh)",
		}},
		want: ("const base::RepeatingCallback<void(int32_t,\n" +
			"                                   int64_t,\n" +
			"                                   const std::tuple<std::string, base::ScopedFD>&)>&"),
	}}

	for _, tc := range cases {
		got, err := makeSignalCallbackType(tc.args)
		if err != nil {
			t.Errorf("Unexpected signal callback type format error: %v", err)
		} else if got != tc.want {
			t.Errorf("Unexpected signal callback type format: got %v, want %v", got, tc.want)
		}
	}
}
