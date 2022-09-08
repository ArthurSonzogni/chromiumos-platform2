// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package introspect_test

import (
	"go.chromium.org/chromiumos/dbusbindings/dbustype"
	"go.chromium.org/chromiumos/dbusbindings/introspect"
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
					{Name: "org.chromium.DBus.Method.Kind", Value: "simple"},
				},
			},
			want: introspect.MethodKindSimple,
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
			want: introspect.MethodKindNormal,
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

func TestMethodArgMethods(t *testing.T) {
	cases := []struct {
		receiver          introspect.MethodArg
		BaseTypeExtract   string
		BaseTypeAppend    string
		InArgTypeAdaptor  string
		InArgTypeProxy    string
		OutArgTypeAdaptor string
		OutArgTypeProxy   string
	}{
		{
			receiver: introspect.MethodArg{
				Name: "arg1",
				Type: "ay",
				Annotation: introspect.Annotation{
					Name:  "org.chromium.DBus.Argument.ProtobufClass",
					Value: "MyProtobufClass",
				},
			},
			BaseTypeExtract:   "MyProtobufClass",
			BaseTypeAppend:    "MyProtobufClass",
			InArgTypeAdaptor:  "const MyProtobufClass&",
			InArgTypeProxy:    "const MyProtobufClass&",
			OutArgTypeAdaptor: "MyProtobufClass*",
			OutArgTypeProxy:   "MyProtobufClass*",
		}, {
			receiver: introspect.MethodArg{
				Name: "arg2",
				Type: "h",
			},
			BaseTypeExtract:   "base::ScopedFD",
			BaseTypeAppend:    "brillo::dbus_utils::FileDescriptor",
			InArgTypeAdaptor:  "const base::ScopedFD&",
			InArgTypeProxy:    "const brillo::dbus_utils::FileDescriptor&",
			OutArgTypeAdaptor: "brillo::dbus_utils::FileDescriptor*",
			OutArgTypeProxy:   "base::ScopedFD*",
		},
	}

	for _, tc := range cases {
		got, err := tc.receiver.BaseType(dbustype.DirectionExtract)
		if err != nil {
			t.Fatalf("getting the base type of %q got error, want nil\ndirection: DirectionExtract\n%s", tc.receiver.Name, err)
		}
		if got != tc.BaseTypeExtract {
			t.Fatalf("getting the base type of %q failed; want %s, got %s", tc.receiver.Name, tc.BaseTypeExtract, got)
		}
		got, err = tc.receiver.BaseType(dbustype.DirectionAppend)
		if err != nil {
			t.Fatalf("getting the base type of %q got error, want nil\ndirection: DirectionAppend\n%s", tc.receiver.Name, err)
		}
		if got != tc.BaseTypeAppend {
			t.Fatalf("getting the base type of %q failed; want %s, got %s", tc.receiver.Name, tc.BaseTypeAppend, got)
		}
		got, err = tc.receiver.InArgType(dbustype.ReceiverAdaptor)
		if err != nil {
			t.Fatalf("getting the in arg type of %q got error, want nil\nreceiver: ReceiverAdaptor\n%s", tc.receiver.Name, err)
		}
		if got != tc.InArgTypeAdaptor {
			t.Fatalf("getting the in arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.InArgTypeAdaptor, got)
		}
		got, err = tc.receiver.InArgType(dbustype.ReceiverProxy)
		if err != nil {
			t.Fatalf("getting the in arg type of %q got error, want nil\nreceiver: ReceiverProxy\n%s", tc.receiver.Name, err)
		}
		if got != tc.InArgTypeProxy {
			t.Fatalf("getting the in arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.InArgTypeProxy, got)
		}
		got, err = tc.receiver.OutArgType(dbustype.ReceiverAdaptor)
		if err != nil {
			t.Fatalf("getting the out arg type of %q got error, want nil\nreceiver: ReceiverAdaptor\n%s", tc.receiver.Name, err)
		}
		if got != tc.OutArgTypeAdaptor {
			t.Fatalf("getting the out arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.OutArgTypeAdaptor, got)
		}
		got, err = tc.receiver.OutArgType(dbustype.ReceiverProxy)
		if err != nil {
			t.Fatalf("getting the out arg type of %q got error, want nil\nreceiver: ReceiverProxy\n%s", tc.receiver.Name, err)
		}
		if got != tc.OutArgTypeProxy {
			t.Fatalf("getting the out arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.OutArgTypeProxy, got)
		}
	}
}

func TestSignalArgMethods(t *testing.T) {
	cases := []struct {
		receiver          introspect.SignalArg
		BaseTypeExtract   string
		BaseTypeAppend    string
		InArgTypeAdaptor  string
		InArgTypeProxy    string
		OutArgTypeAdaptor string
		OutArgTypeProxy   string
	}{
		{
			receiver: introspect.SignalArg{
				Name: "arg3",
				Type: "ay",
				Annotation: introspect.Annotation{
					Name:  "org.chromium.DBus.Argument.ProtobufClass",
					Value: "MyProtobufClass",
				},
			},
			BaseTypeExtract:   "MyProtobufClass",
			BaseTypeAppend:    "MyProtobufClass",
			InArgTypeAdaptor:  "const MyProtobufClass&",
			InArgTypeProxy:    "const MyProtobufClass&",
			OutArgTypeAdaptor: "MyProtobufClass*",
			OutArgTypeProxy:   "MyProtobufClass*",
		}, {
			receiver: introspect.SignalArg{
				Name: "arg4",
				Type: "h",
			},
			BaseTypeExtract:   "base::ScopedFD",
			BaseTypeAppend:    "brillo::dbus_utils::FileDescriptor",
			InArgTypeAdaptor:  "const base::ScopedFD&",
			InArgTypeProxy:    "const brillo::dbus_utils::FileDescriptor&",
			OutArgTypeAdaptor: "brillo::dbus_utils::FileDescriptor*",
			OutArgTypeProxy:   "base::ScopedFD*",
		},
	}

	for _, tc := range cases {
		got, err := tc.receiver.BaseType(dbustype.DirectionExtract)
		if err != nil {
			t.Fatalf("getting the base type of %q got error, want nil\ndirection: DirectionExtract\n%s", tc.receiver.Name, err)
		}
		if got != tc.BaseTypeExtract {
			t.Fatalf("getting the base type of %q failed; want %s, got %s", tc.receiver.Name, tc.BaseTypeExtract, got)
		}
		got, err = tc.receiver.BaseType(dbustype.DirectionAppend)
		if err != nil {
			t.Fatalf("getting the base type of %q got error, want nil\ndirection: DirectionAppend\n%s", tc.receiver.Name, err)
		}
		if got != tc.BaseTypeAppend {
			t.Fatalf("getting the base type of %q failed; want %s, got %s", tc.receiver.Name, tc.BaseTypeAppend, got)
		}
		got, err = tc.receiver.InArgType(dbustype.ReceiverAdaptor)
		if err != nil {
			t.Fatalf("getting the in arg type of %q got error, want nil\nreceiver: ReceiverAdaptor\n%s", tc.receiver.Name, err)
		}
		if got != tc.InArgTypeAdaptor {
			t.Fatalf("getting the in arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.InArgTypeAdaptor, got)
		}
		got, err = tc.receiver.InArgType(dbustype.ReceiverProxy)
		if err != nil {
			t.Fatalf("getting the in arg type of %q got error, want nil\nreceiver: ReceiverProxy\n%s", tc.receiver.Name, err)
		}
		if got != tc.InArgTypeProxy {
			t.Fatalf("getting the in arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.InArgTypeProxy, got)
		}
		got, err = tc.receiver.OutArgType(dbustype.ReceiverAdaptor)
		if err != nil {
			t.Fatalf("getting the out arg type of %q got error, want nil\nreceiver: ReceiverAdaptor\n%s", tc.receiver.Name, err)
		}
		if got != tc.OutArgTypeAdaptor {
			t.Fatalf("getting the out arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.OutArgTypeAdaptor, got)
		}
		got, err = tc.receiver.OutArgType(dbustype.ReceiverProxy)
		if err != nil {
			t.Fatalf("getting the out arg type of %q got error, want nil\nreceiver: ReceiverProxy\n%s", tc.receiver.Name, err)
		}
		if got != tc.OutArgTypeProxy {
			t.Fatalf("getting the out arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.OutArgTypeProxy, got)
		}
	}
}

func TestPropertyMethods(t *testing.T) {
	cases := []struct {
		receiver          introspect.Property
		BaseTypeExtract   string
		BaseTypeAppend    string
		InArgTypeAdaptor  string
		InArgTypeProxy    string
		OutArgTypeAdaptor string
		OutArgTypeProxy   string
	}{
		{
			receiver: introspect.Property{
				Name: "property1",
				Type: "h",
			},
			BaseTypeExtract:   "base::ScopedFD",
			BaseTypeAppend:    "brillo::dbus_utils::FileDescriptor",
			InArgTypeAdaptor:  "const base::ScopedFD&",
			InArgTypeProxy:    "const brillo::dbus_utils::FileDescriptor&",
			OutArgTypeAdaptor: "brillo::dbus_utils::FileDescriptor*",
			OutArgTypeProxy:   "base::ScopedFD*",
		},
	}

	for _, tc := range cases {
		got, err := tc.receiver.BaseType(dbustype.DirectionExtract)
		if err != nil {
			t.Fatalf("getting the base type of %q got error, want nil\ndirection: DirectionExtract\n%s", tc.receiver.Name, err)
		}
		if got != tc.BaseTypeExtract {
			t.Fatalf("getting the base type of %q failed; want %s, got %s", tc.receiver.Name, tc.BaseTypeExtract, got)
		}
		got, err = tc.receiver.BaseType(dbustype.DirectionAppend)
		if err != nil {
			t.Fatalf("getting the base type of %q got error, want nil\ndirection: DirectionAppend\n%s", tc.receiver.Name, err)
		}
		if got != tc.BaseTypeAppend {
			t.Fatalf("getting the base type of %q failed; want %s, got %s", tc.receiver.Name, tc.BaseTypeAppend, got)
		}
		got, err = tc.receiver.InArgType(dbustype.ReceiverAdaptor)
		if err != nil {
			t.Fatalf("getting the in arg type of %q got error, want nil\nreceiver: ReceiverAdaptor\n%s", tc.receiver.Name, err)
		}
		if got != tc.InArgTypeAdaptor {
			t.Fatalf("getting the in arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.InArgTypeAdaptor, got)
		}
		got, err = tc.receiver.InArgType(dbustype.ReceiverProxy)
		if err != nil {
			t.Fatalf("getting the in arg type of %q got error, want nil\nreceiver: ReceiverProxy\n%s", tc.receiver.Name, err)
		}
		if got != tc.InArgTypeProxy {
			t.Fatalf("getting the in arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.InArgTypeProxy, got)
		}
		got, err = tc.receiver.OutArgType(dbustype.ReceiverAdaptor)
		if err != nil {
			t.Fatalf("getting the out arg type of %q got error, want nil\nreceiver: ReceiverAdaptor\n%s", tc.receiver.Name, err)
		}
		if got != tc.OutArgTypeAdaptor {
			t.Fatalf("getting the out arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.OutArgTypeAdaptor, got)
		}
		got, err = tc.receiver.OutArgType(dbustype.ReceiverProxy)
		if err != nil {
			t.Fatalf("getting the out arg type of %q got error, want nil\nreceiver: ReceiverProxy\n%s", tc.receiver.Name, err)
		}
		if got != tc.OutArgTypeProxy {
			t.Fatalf("getting the out arg type of %q failed; want %s, got %s", tc.receiver.Name, tc.OutArgTypeProxy, got)
		}
	}
}
