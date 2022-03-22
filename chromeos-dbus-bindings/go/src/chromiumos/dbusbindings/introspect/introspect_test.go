// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package introspect_test

import (
	"chromiumos/dbusbindings/introspect"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestInputArguments(t *testing.T) {
	m := introspect.Method{
		Name: "f",
		Args: []introspect.MethodArg{
			{Name: "x1", Direction: "in", Type: "i"},
			{Name: "x2", Direction: "", Type: "i"},
			{Name: "x3", Direction: "out", Type: "i"},
		},
	}
	got := m.InputArguments()
	want := []introspect.MethodArg{
		{Name: "x1", Direction: "in", Type: "i"},
		{Name: "x2", Direction: "", Type: "i"},
	}
	if diff := cmp.Diff(got, want); diff != "" {
		t.Errorf("InputArguments failed (-got +want):\n%s", diff)
	}
}
func TestOutputArguments(t *testing.T) {
	m := introspect.Method{
		Name: "f",
		Args: []introspect.MethodArg{
			{Name: "x1", Direction: "in", Type: "i"},
			{Name: "x2", Direction: "", Type: "i"},
			{Name: "x3", Direction: "out", Type: "i"},
		},
	}
	got := m.OutputArguments()
	want := []introspect.MethodArg{
		{Name: "x3", Direction: "out", Type: "i"},
	}
	if diff := cmp.Diff(got, want); diff != "" {
		t.Errorf("OutputArguments failed (-got +want):\n%s", diff)
	}
}
func TestKind(t *testing.T) {
	cases := []struct {
		input introspect.Method
		want  introspect.MethodKind
	}{
		{
			input: introspect.Method{
				Name: "f1",
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "normal"},
				},
			},
			want: introspect.MethodKindNormal,
		}, {
			input: introspect.Method{
				Name: "f2",
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Kind", Value: "raw"},
				},
			},
			want: introspect.MethodKindRaw,
		}, {
			input: introspect.Method{
				Name: "f3",
				Annotations: []introspect.Annotation{
					{Name: "org.freedesktop.DBus.GLib.Async"},
				},
			},
			want: introspect.MethodKindAsync,
		}, {
			input: introspect.Method{
				Name: "f4",
			},
			want: introspect.MethodKindSimple,
		},
	}
	for _, tc := range cases {
		got := tc.input.Kind()
		if got != tc.want {
			t.Errorf("Kind faild, method name is %s\n got %q, want %q", tc.input.Name, got, tc.want)
		}
	}
}
func TestIncludeDBusMessage(t *testing.T) {
	cases := []struct {
		input introspect.Method
		want  bool
	}{
		{
			input: introspect.Method{
				Name: "f1",
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.IncludeDBusMessage", Value: "true"},
				},
			},
			want: true,
		}, {
			input: introspect.Method{
				Name: "f2",
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.IncludeDBusMessage", Value: "false"},
				},
			},
			want: false,
		}, {
			input: introspect.Method{
				Name: "f3",
			},
			want: false,
		},
	}
	for _, tc := range cases {
		got := tc.input.IncludeDBusMessage()
		if got != tc.want {
			t.Errorf("IncludeDBusMessage faild, method name is %s\n got %t, want %t", tc.input.Name, got, tc.want)
		}
	}
}
func TestConst(t *testing.T) {
	cases := []struct {
		input introspect.Method
		want  bool
	}{
		{
			input: introspect.Method{
				Name: "f1",
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Const", Value: "true"},
				},
			},
			want: true,
		}, {
			input: introspect.Method{
				Name: "f2",
				Annotations: []introspect.Annotation{
					{Name: "org.chromium.DBus.Method.Const", Value: "false"},
				},
			},
			want: false,
		}, {
			input: introspect.Method{
				Name: "f3",
			},
			want: false,
		},
	}
	for _, tc := range cases {
		got := tc.input.Const()
		if got != tc.want {
			t.Errorf("Const faild, method name is %s\n got %t, want %t", tc.input.Name, got, tc.want)
		}
	}
}
