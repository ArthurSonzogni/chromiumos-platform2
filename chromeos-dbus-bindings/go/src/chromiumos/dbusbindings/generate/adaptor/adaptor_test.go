// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package adaptor

import (
	"bytes"
	"testing"
	"text/template"

	"chromiumos/dbusbindings/introspect"

	"github.com/google/go-cmp/cmp"
)

const (
	generateAdaptorsOutput = `// Automatic generation of D-Bus interfaces:
//  - org.chromium.Test
//  - org.chromium.Test2
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR_H
#define ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR_H
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/scoped_file.h>
#include <dbus/object_path.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/variant_dictionary.h>

namespace org {
namespace chromium {

// Interface definition for org::chromium::Test.
class TestInterface {
 public:
  virtual ~TestInterface() = default;

  virtual std::string f() = 0;
};

// Interface adaptor for org::chromium::Test.
class TestAdaptor {
 public:
  TestAdaptor(TestInterface* interface) : interface_(interface) {}
  TestAdaptor(const TestAdaptor&) = delete;
  TestAdaptor& operator=(const TestAdaptor&) = delete;

  void RegisterWithDBusObject(brillo::dbus_utils::DBusObject* object) {
    brillo::dbus_utils::DBusInterface* itf =
        object->AddOrGetInterface("org.chromium.Test");
  }

  static dbus::ObjectPath GetObjectPath() {
    return dbus::ObjectPath{"/org/chromium/Test"};
  }

  static const char* GetIntrospectionXml() {
    return
        "  <interface name=\"org.chromium.Test\">\n"
        "  </interface>\n";
  }

 private:
  TestInterface* interface_;  // Owned by container of this adapter.
};

}  // namespace chromium
}  // namespace org

namespace org {
namespace chromium {

// Interface definition for org::chromium::Test2.
class Test2Interface {
 public:
  virtual ~Test2Interface() = default;
};

// Interface adaptor for org::chromium::Test2.
class Test2Adaptor {
 public:
  Test2Adaptor(Test2Interface* /* interface */) {}
  Test2Adaptor(const Test2Adaptor&) = delete;
  Test2Adaptor& operator=(const Test2Adaptor&) = delete;

  void RegisterWithDBusObject(brillo::dbus_utils::DBusObject* object) {
    brillo::dbus_utils::DBusInterface* itf =
        object->AddOrGetInterface("org.chromium.Test2");
  }

  static const char* GetIntrospectionXml() {
    return
        "  <interface name=\"org.chromium.Test2\">\n"
        "  </interface>\n";
  }

 private:
};

}  // namespace chromium
}  // namespace org


// Interface definition for EmptyInterface.
class EmptyInterfaceInterface {
 public:
  virtual ~EmptyInterfaceInterface() = default;
};

// Interface adaptor for EmptyInterface.
class EmptyInterfaceAdaptor {
 public:
  EmptyInterfaceAdaptor(EmptyInterfaceInterface* /* interface */) {}
  EmptyInterfaceAdaptor(const EmptyInterfaceAdaptor&) = delete;
  EmptyInterfaceAdaptor& operator=(const EmptyInterfaceAdaptor&) = delete;

  void RegisterWithDBusObject(brillo::dbus_utils::DBusObject* object) {
    brillo::dbus_utils::DBusInterface* itf =
        object->AddOrGetInterface("EmptyInterface");
  }

  static const char* GetIntrospectionXml() {
    return
        "  <interface name=\"EmptyInterface\">\n"
        "  </interface>\n";
  }

 private:
};

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR_H
`

	newFileDescriptorsOutput = `// Automatic generation of D-Bus interfaces:
//  - org.chromium.Test
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR2_H
#define ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR2_H
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/scoped_file.h>
#include <dbus/object_path.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/variant_dictionary.h>

namespace org {
namespace chromium {

// Interface definition for org::chromium::Test.
class TestInterface {
 public:
  virtual ~TestInterface() = default;
};

// Interface adaptor for org::chromium::Test.
class TestAdaptor {
 public:
  TestAdaptor(TestInterface* /* interface */) {}
  TestAdaptor(const TestAdaptor&) = delete;
  TestAdaptor& operator=(const TestAdaptor&) = delete;

  void RegisterWithDBusObject(brillo::dbus_utils::DBusObject* object) {
    brillo::dbus_utils::DBusInterface* itf =
        object->AddOrGetInterface("org.chromium.Test");
  }

  static dbus::ObjectPath GetObjectPath() {
    return dbus::ObjectPath{"/org/chromium/Test"};
  }

  static const char* GetIntrospectionXml() {
    return
        "  <interface name=\"org.chromium.Test\">\n"
        "  </interface>\n";
  }

 private:
};

}  // namespace chromium
}  // namespace org
#endif  // ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR2_H
`
)

func TestGenerateAdaptors(t *testing.T) {
	itf := introspect.Interface{
		Name: "org.chromium.Test",
		Methods: []introspect.Method{
			{
				Name: "f",
				Args: []introspect.MethodArg{
					{Type: "s", Direction: "out"},
				},
			},
		},
	}

	itf2 := introspect.Interface{
		Name: "org.chromium.Test2",
	}

	itf3 := introspect.Interface{
		Name: "EmptyInterface",
	}

	introspections := []introspect.Introspection{
		{
			Name:       "/org/chromium/Test",
			Interfaces: []introspect.Interface{itf},
		}, {
			Interfaces: []introspect.Interface{itf2},
		}, {
			Interfaces: []introspect.Interface{itf3},
		},
	}

	out := new(bytes.Buffer)
	if err := Generate(introspections, out, "/tmp/adaptor.h"); err != nil {
		t.Errorf("Generate got error, want nil: %v", err)
	}

	if diff := cmp.Diff(out.String(), generateAdaptorsOutput); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

// Note that new-style FD bindings use brillo::dbus_utils::FileDescriptor and base::ScopedFD.
func TestGenerateAdaptorIncludingNewFileDescriptors(t *testing.T) {
	itf := introspect.Interface{
		Name: "org.chromium.Test",
	}

	introspections := []introspect.Introspection{
		{
			Name:       "/org/chromium/Test",
			Interfaces: []introspect.Interface{itf},
		},
	}

	out := new(bytes.Buffer)
	if err := Generate(introspections, out, "/tmp/adaptor2.h"); err != nil {
		t.Errorf("Generate got error, want nil: %v", err)
	}

	if diff := cmp.Diff(out.String(), newFileDescriptorsOutput); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestInterfaceMethodsTempl(t *testing.T) {
	cases := []struct {
		input introspect.Interface
		want  string
	}{
		{
			input: introspect.Interface{
				Name: "itfWithNoMethod",
			},
			want: "",
		}, {
			input: introspect.Interface{
				Name: "itfWithMethodsWithComment",
				Methods: []introspect.Method{
					{
						Name:      "methodWithComment1",
						DocString: "this is comment1",
					}, {
						Name:      "methodWithComment2",
						DocString: "this is comment2",
					},
				},
			},
			want: `
  // this is comment1
  virtual void methodWithComment1() = 0;
  // this is comment2
  virtual void methodWithComment2() = 0;
`,
		}, {
			input: introspect.Interface{
				Name: "itfWithMethodWithNoArg",
				Methods: []introspect.Method{
					{
						Name: "methodWithNoArg",
						Args: []introspect.MethodArg{
							{Name: "onlyOutput", Direction: "out", Type: "i"},
						},
						Annotations: []introspect.Annotation{
							{Name: "org.chromium.DBus.Method.Kind", Value: "simple"},
						},
					},
				},
			},
			want: `
  virtual int32_t methodWithNoArg() = 0;
`,
		}, {
			input: introspect.Interface{
				Name: "itfWithMethodWithArgs",
				Methods: []introspect.Method{
					{
						Name: "methodWithArgs",
						Args: []introspect.MethodArg{
							{Name: "n", Direction: "in", Type: "i"},
							{Name: "", Direction: "in", Type: "s"},
						},
					},
				},
			},
			want: `
  virtual void methodWithArgs(
      int32_t in_n,
      const std::string& in_2) = 0;
`,
		}, {
			input: introspect.Interface{
				Name: "itfWithConstMethod",
				Methods: []introspect.Method{
					{
						Name: "methodWithArgs",
						Args: []introspect.MethodArg{
							{Name: "n", Direction: "in", Type: "i"},
						},
						Annotations: []introspect.Annotation{
							{Name: "org.chromium.DBus.Method.Const", Value: "true"},
						},
					},
				},
			},
			want: `
  virtual void methodWithArgs(
      int32_t in_n) const = 0;
`,
		},
	}

	tmpl := template.Must(template.New("interfaceMethodsTempl").Funcs(funcMap).Parse(`{{template "interfaceMethods" .}}`))
	if _, err := tmpl.Parse(interfaceMethodsTempl); err != nil {
		t.Fatalf("InterfaceMethodsTempl parse got error, want nil: %v", err)
	}

	for _, tc := range cases {
		out := new(bytes.Buffer)
		if err := tmpl.Execute(out, tc.input); err != nil {
			t.Fatalf("InterfaceMethodsTempl execute got error, want nil: %v", err)
		}
		if diff := cmp.Diff(out.String(), tc.want); diff != "" {
			t.Errorf("InterfaceMethodsTempl execute faild, interface name is %s\n(-got +want):\n%s", tc.input.Name, diff)
		}
	}
}
