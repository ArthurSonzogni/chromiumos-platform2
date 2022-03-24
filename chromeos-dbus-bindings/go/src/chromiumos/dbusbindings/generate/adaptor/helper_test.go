// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package adaptor

import (
	"chromiumos/dbusbindings/introspect"
	"testing"

	"github.com/google/go-cmp/cmp"
)

// TODO(chromium:983008): After implementing signature function contents, fix types of want.

func TestMakeMethodRetType(t *testing.T) {
	cases := []struct {
		input introspect.Method
		want  string
	}{
		{
			input: introspect.Method{
				Name: "simpleMethodWithOnlyOutputArg",
				Args: []introspect.MethodArg{
					{Name: "onlyOutput", Direction: "out", Type: "i"},
				},
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "simple"},
				},
			},
			// TODO(chromium:983008): After implementing dbustype package, fix to int32_t
			want: "i",
		}, {
			input: introspect.Method{
				Name: "normalMethod",
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "normal"},
				},
			},
			want: "bool",
		}, {
			input: introspect.Method{
				Name: "theOtherMethods",
			},
			want: "void",
		},
	}
	for _, tc := range cases {
		got, err := makeMethodRetType(tc.input)
		if err != nil {
			t.Errorf("makeMethodRetType got error, want nil: %v", err)
		}
		if got != tc.want {
			t.Errorf("makeMethodRetType faild, method name is %s\ngot %q, want %q", tc.input.Name, got, tc.want)
		}
	}
}

func TestMakeMethodArgs(t *testing.T) {
	cases := []struct {
		input introspect.Method
		want  []string
	}{
		{
			input: introspect.Method{
				Name: "simpleMethodWithOnlyOutputArg",
				Args: []introspect.MethodArg{
					{Name: "onlyOutput", Direction: "out", Type: "i"},
				},
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "simple"},
				},
			},
			want: nil,
		}, {
			input: introspect.Method{
				Name: "theOtherSimpleMethod",
				Args: []introspect.MethodArg{
					{Name: "x", Direction: "in", Type: "i"},
				},
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "simple"},
				},
			},
			// TODO(chromium:983008): After implementing dbustype package, fix i to int32_t
			want: []string{"i in_x"},
		}, {
			input: introspect.Method{
				Name: "normalMethod",
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "normal"},
				},
			},
			want: []string{"brillo::ErrorPtr* error"},
		}, {
			input: introspect.Method{
				Name: "normalMethodIncludingMessage",
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "normal"},
					{Name: "org.chromium.DBus.Method.IncludeDBusMessage", Value: "true"},
				},
			},
			want: []string{"brillo::ErrorPtr* error", "dbus::Message* message"},
		}, {
			input: introspect.Method{
				Name: "asyncMethod",
				Args: []introspect.MethodArg{
					{Name: "x1", Direction: "out", Type: "i"},
					{Name: "x2", Direction: "out", Type: "s"},
				},
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "async"},
				},
			},
			// TODO(chromium:983008): After implementing dbustype package, fix <i, i> to <int32_t, std::string>
			want: []string{"std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<i, i>> response"},
		}, {
			input: introspect.Method{
				Name: "asyncMethodWithNoArg",
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "async"},
				},
			},
			want: []string{"std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response"},
		}, {
			input: introspect.Method{
				Name: "asyncMethodIncludingMessage",
				Args: []introspect.MethodArg{
					{Name: "x", Direction: "out", Type: "i"},
				},
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "async"},
					{Name: "org.chromium.DBus.Method.IncludeDBusMessage", Value: "true"},
				},
			},
			want: []string{
				// TODO(chromium:983008): After implementing dbustype package, fix i to int32_t
				"std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<i>> response",
				"dbus::Message* message",
			},
		}, {
			input: introspect.Method{
				Name: "rawMethod",
				Args: []introspect.MethodArg{
					{Name: "x", Direction: "in", Type: "i"},
					{Name: "ret", Direction: "in", Type: "b"},
				},
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "raw"},
				},
			},
			want: []string{
				"dbus::MethodCall* method_call",
				"brillo::dbus_utils::ResponseSender sender",
			},
		}, {
			input: introspect.Method{
				Name: "methodWithArgs",
				Args: []introspect.MethodArg{
					{Name: "", Direction: "in", Type: "b"},
					{Name: "x", Direction: "in", Type: "d"},
					{Name: "n", Direction: "out", Type: "i"},
					{Name: "", Direction: "out", Type: "u"},
				},
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "simple"},
				},
			},
			want: []string{
				// TODO(chromium:983008): After implementing dbustype package,
				// fix to "bool in_1", "double in_x", "int32_t out_n", "uint32_t out_4"
				"i in_1", "i in_x", "i out_n", "i out_4",
			},
		},
	}
	for _, tc := range cases {
		got, err := makeMethodArgs(tc.input)
		if err != nil {
			t.Errorf("makeMethodArgs got error, want nil: %v", err)
		}
		if diff := cmp.Diff(got, tc.want); diff != "" {
			t.Errorf("makeMethodArgs failed, method name is %s\n(-got +want):\n%s", tc.input.Name, diff)
		}
	}
}
