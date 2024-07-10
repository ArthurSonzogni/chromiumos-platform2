// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package adaptor outputs a adaptor based on introspects.
package adaptor

import (
	"io"
	"text/template"

	"go.chromium.org/chromiumos/dbusbindings/generate/genutil"
	"go.chromium.org/chromiumos/dbusbindings/introspect"
)

type templateArgs struct {
	Introspects      []introspect.Introspection
	HeaderGuard      string
	UseAdaptorMethod bool
}

type registerWithDBusObjectArgs struct {
	Interface        introspect.Interface
	UseAdaptorMethod bool
}

type adaptorMethodsArgs struct {
	Methods          []introspect.Method
	UseAdaptorMethod bool
}

var funcMap = template.FuncMap{
	"extractProtoIncludes":    genutil.ExtractProtoIncludes,
	"makeInterfaceName":       genutil.MakeInterfaceName,
	"makeAdaptorName":         genutil.MakeAdaptorName,
	"makeFullItfName":         genutil.MakeFullItfName,
	"extractNameSpaces":       genutil.ExtractNameSpaces,
	"formatComment":           genutil.FormatComment,
	"makeMethodRetType":       makeMethodRetType,
	"makeMethodParams":        makeMethodParams,
	"makeAddHandlerName":      makeAddHandlerName,
	"makePropertyWriteAccess": makePropertyWriteAccess,
	"makeVariableName":        genutil.MakeVariableName,
	"makeSignalParams":        makeSignalParams,
	"makeSignalArgNames":      makeSignalArgNames,
	"makePropertyVariableName": func(p *introspect.Property) string {
		return p.VariableName()
	},
	"makePropertyBaseTypeExtract": func(p *introspect.Property) (string, error) {
		return p.BaseType()
	},
	"makePropertyInArgTypeAdaptor": func(p *introspect.Property) (string, error) {
		return p.InArgType()
	},
	"makeDBusSignalParams": makeDBusSignalParams,
	"packAdaptorMethodsArgs": func(ms []introspect.Method, useAdaptorMethod bool) adaptorMethodsArgs {
		return adaptorMethodsArgs{ms, useAdaptorMethod}
	},
	"packRegisterWithDBusObjectArgs": func(itf introspect.Interface, useAdaptorMethod bool) registerWithDBusObjectArgs {
		return registerWithDBusObjectArgs{itf, useAdaptorMethod}
	},
	"reverse": genutil.Reverse,
}

const (
	templateText = `// Automatic generation of D-Bus interfaces:
{{- range .Introspects}}{{range .Interfaces}}
//  - {{.Name}}
{{- end}}{{end}}

#ifndef {{.HeaderGuard}}
#define {{.HeaderGuard}}
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/scoped_file.h>
#include <dbus/object_path.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_adaptor.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/dbus/utils.h>
#include <brillo/variant_dictionary.h>

{{- $protoIncludes := (extractProtoIncludes .Introspects)}}
{{- if $protoIncludes }}
{{/* empty line */}}
{{- range $include := $protoIncludes}}
#include <{{$include}}>
{{- end}}
{{- end}}

{{- range $introspect := .Introspects}}{{range .Interfaces}}
{{- $itfName := makeInterfaceName .Name}}
{{- $className := makeAdaptorName .Name}}
{{- $fullItfName := makeFullItfName .Name}}
{{- if extractNameSpaces .Name}}
{{/* empty line */}}
{{- range extractNameSpaces .Name}}
namespace {{.}} {
{{- end}}
{{- end}}

// Interface definition for {{$fullItfName}}.
{{- formatComment .DocString 0}}
class {{$itfName}} {
 public:
  virtual ~{{$itfName}}() = default;
{{- template "interfaceMethodsTmpl" .}}
};

// Interface adaptor for {{$fullItfName}}.
class {{$className}} {
 public:
{{- if .Methods}}
  {{$className}}({{$itfName}}* interface) : interface_(interface) {}
{{- else}}
  {{$className}}({{$itfName}}* /* interface */) {}
{{- end}}
  {{$className}}(const {{$className}}&) = delete;
  {{$className}}& operator=(const {{$className}}&) = delete;

{{- template "registerWithDBusObjectTmpl" (packRegisterWithDBusObjectArgs . $.UseAdaptorMethod)}}
{{- template "sendSignalMethodsTmpl" .}}
{{- template "propertyMethodImplementationTmpl" .}}
{{- if $introspect.Name}}

  static dbus::ObjectPath GetObjectPath() {
    return dbus::ObjectPath{"{{$introspect.Name}}"};
  }
{{- end}}
{{- template "quotedIntrospectionForInterfaceTmpl" .}}

 private:
{{- template "adaptorMethodsTmpl" (packAdaptorMethodsArgs .Methods $.UseAdaptorMethod)}}
{{- template "signalDataMembersTmpl" .}}
{{- template "propertyDataMembersTmpl" .}}
{{- if .Methods}}

  {{$itfName}}* interface_;  // Owned by container of this adapter.
{{- end}}
};

{{- if extractNameSpaces .Name}}
{{/* empty line */}}
{{- range extractNameSpaces .Name | reverse}}
}  // namespace {{.}}
{{- end}}
{{- end}}
{{- end}}{{end}}

#endif  // {{.HeaderGuard}}
`
	interfaceMethodsTmpl = `{{- define "interfaceMethodsTmpl"}}
{{- if .Methods}}
{{/* empty line */}}
{{- range .Methods}}
  {{- formatComment .DocString 2}}
  virtual {{makeMethodRetType .}} {{.Name}}(
{{- range $i, $arg := makeMethodParams .}}{{if ne $i 0}},{{end}}
      {{$arg}}
{{- end}}) {{if .Const}}const {{end}}= 0;
{{- end}}
{{- end}}
{{- end}}`

	registerWithDBusObjectTmpl = `{{- define "registerWithDBusObjectTmpl"}}
{{- with .Interface}}
{{- $className := makeAdaptorName .Name}}
{{- $itfName := makeInterfaceName .Name}}

  void RegisterWithDBusObject(brillo::dbus_utils::DBusObject* object) {
    brillo::dbus_utils::DBusInterface* itf =
        object->AddOrGetInterface("{{.Name}}");
{{- if .Methods}}
{{/* empty line */}}
    {{- range .Methods}}
    {{- if $.UseAdaptorMethod}}
    itf->AddRawMethodHandler(
        "{{.Name}}",
        base::BindRepeating(
            &{{$className}}::Adaptor{{.Name}}, base::Unretained(this)));
    {{- else}}
    itf->{{makeAddHandlerName .}}(
        "{{.Name}}",
        base::Unretained(interface_),
        &{{$itfName}}::{{.Name}});
    {{- end}}
    {{- end}}
{{- end}}

{{- if .Signals}}
{{/* empty line */}}
{{- range .Signals}}
    signal_{{.Name}}_ = itf->RegisterSignalOfType<Signal{{.Name}}Type>("{{.Name}}");
{{- end}}
{{- end}}

{{- $adaptorName := makeAdaptorName .Name}}
{{- if .Properties}}
{{/* empty line */}}
{{- range .Properties}}
  {{- $writeAccess := makePropertyWriteAccess .}}
  {{- $variableName := makePropertyVariableName . | makeVariableName}}
  {{- if $writeAccess}}
    {{- /* Register exported properties. */}}
    {{$variableName}}_.SetAccessMode(
        brillo::dbus_utils::ExportedPropertyBase::Access::{{$writeAccess}});
    {{$variableName}}_.SetValidator(
        base::BindRepeating(&{{$adaptorName}}::Validate{{.Name}},
                            base::Unretained(this)));
  {{- end}}
    itf->AddProperty({{.Name}}Name(), &{{$variableName}}_);
{{- end}}
{{- end}}
  }
{{- end}}
{{- end}}`

	sendSignalMethodsTmpl = `{{- define "sendSignalMethodsTmpl"}}
{{- if .Signals}}
{{/* empty line */}}
{{- range .Signals}}
  {{- formatComment .DocString 2}}
  void Send{{.Name}}Signal(
{{- range $i, $arg := makeSignalParams .}}{{if ne $i 0}},{{end}}
      {{$arg}}
{{- end}}) {
    auto signal = signal_{{.Name}}_.lock();
    if (signal)
      signal->Send({{makeSignalArgNames .}});
  }
{{- end}}
{{- end}}
{{- end}}`

	propertyMethodImplementationTmpl = `{{- define "propertyMethodImplementationTmpl"}}
{{- if .Properties}}
{{- range .Properties}}
  {{- $baseType := makePropertyBaseTypeExtract .}}
  {{- $variableName := makePropertyVariableName . | makeVariableName}}
{{/* empty line */}}
  {{- /* Property name accessor. */}}
  {{- formatComment .DocString 2}}
  static const char* {{.Name}}Name() { return "{{.Name}}"; }

  {{- /* Getter method. */}}
  {{$baseType}} Get{{.Name}}() const {
    return {{$variableName}}_.GetValue().Get<{{$baseType}}>();
  }

  {{- /* Setter method. */}}
  void Set{{.Name}}({{makePropertyInArgTypeAdaptor .}} {{$variableName}}) {
    {{$variableName}}_.SetValue({{$variableName}});
  }

  {{- /* Validation method for property with write access. */}}
{{- if ne .Access "read"}}
  virtual bool Validate{{.Name}}(
      {{- /* Explicitly specify the "value" parameter as const & to match the */}}
      {{- /* validator callback function signature. */}}
      brillo::ErrorPtr* /*error*/, const {{$baseType}}& /*value*/) {
    return true;
  }
{{- end}}
{{- end}}
{{- end}}
{{- end}}`

	quotedIntrospectionForInterfaceTmpl = `{{- define "quotedIntrospectionForInterfaceTmpl"}}

  static const char* GetIntrospectionXml() {
    return
        "  <interface name=\"{{.Name}}\">\n"
{{- range .Methods}}
        "    <method name=\"{{.Name}}\">\n"
  {{- range .InputArguments}}
        "      <arg name=\"{{.Name}}\" type=\"{{.Type}}\" direction=\"in\"/>\n"
  {{- end}}
  {{- range .OutputArguments}}
        "      <arg name=\"{{.Name}}\" type=\"{{.Type}}\" direction=\"out\"/>\n"
  {{- end}}
        "    </method>\n"
{{- end}}
{{- range .Signals}}
        "    <signal name=\"{{.Name}}\">\n"
  {{- range .Args}}
        "      <arg name=\"{{.Name}}\" type=\"{{.Type}}\"/>\n"
  {{- end}}
        "    </signal>\n"
{{- end}}
        "  </interface>\n";
  }
{{- end}}`

	adaptorMethodsTmpl = `{{- define "adaptorMethodsTmpl"}}
{{- if .UseAdaptorMethod}}
{{/* empty line */}}
{{- range .Methods}}
  void Adaptor{{.Name}}(::dbus::MethodCall* method_call,
                        ::brillo::dbus_utils::ResponseSender sender) {
    {{- if eq .Kind "raw"}}
    interface_->{{.Name}}(method_call, std::move(sender));
    {{- else if eq .Kind "async"}}
    ::brillo::dbus_utils::details::HandleAsyncDBusMethod<
        std::tuple<{{range $i, $arg := .InputArguments}}{{if ne $i 0}}, {{end}}{{$arg.BaseType}}{{end}}>,
        ::brillo::dbus_utils::DBusMethodResponse<
            {{range $i, $arg := .OutputArguments}}{{if ne $i 0}}, {{end}}{{$arg.BaseType}}{{end}}>
    >(
        method_call,
        std::move(sender),
        [this](auto response, ::dbus::MethodCall* method_call,
                auto&&... args) {
          interface_->{{.Name}}(
              std::move(response),
              {{- if .IncludeDBusMessage}}
              method_call,
              {{- end}}
              std::forward<decltype(args)>(args)...);
        });
    {{- else}}
    ::brillo::dbus_utils::details::HandleSyncDBusMethod<
        std::tuple<{{range $i, $arg := .InputArguments}}{{if ne $i 0}}, {{end}}{{$arg.BaseType}}{{end}}>,
        std::tuple<{{range $i, $arg := .OutputArguments}}{{if ne $i 0}}, {{end}}{{$arg.BaseType}}{{end}}>
    >(
        method_call,
        std::move(sender),
        [this](::dbus::MethodCall* method_call, auto&&... in_args) -> ::base::expected<std::tuple<{{range $i, $arg := .OutputArguments}}{{if ne $i 0}}, {{end}}{{$arg.BaseType}}{{end}}>, ::dbus::Error> {
            std::tuple<{{range $i, $arg := .OutputArguments}}{{if ne $i 0}}, {{end}}{{$arg.BaseType}}{{end}}> output;
            {{- if eq .Kind "simple"}}
            {{- if eq (len .OutputArguments) 1}}
            std::get<0>(output) = interface_->{{.Name}}(
                std::forward<decltype(in_args)>(in_args)...);
            {{- else}}
            std::apply([&](auto&&... out_args) {
                interface_->{{.Name}}(
                    std::forward<decltype(in_args)>(in_args)...,
                    &out_args...);
            }, std::move(output));
            {{- end}}
            {{- else}}{{- /* i.e. eq .Kind "normal" */}}
            ::brillo::ErrorPtr error;
            bool result = std::apply([&](auto&&... out_args) {
                return interface_->{{.Name}}(
                    &error,
                    {{- if .IncludeDBusMessage}}
                    method_call,
                    {{- end}}
                    std::forward<decltype(in_args)>(in_args)...,
                    &out_args...);
            }, std::move(output));
            if (!result) {
              CHECK(error.get());
              return ::base::unexpected(::brillo::dbus_utils::ToDBusError(*error));
            }
            {{- end}}
            return ::base::ok(std::move(output));
        });
    {{- end}}
  }
{{- end}}
{{- end}}
{{- end}}`

	signalDataMembersTmpl = `{{- define "signalDataMembersTmpl"}}
{{- range .Signals}}

  using Signal{{.Name}}Type = brillo::dbus_utils::DBusSignal<
{{- range $i, $arg := makeDBusSignalParams .}}{{if ne $i 0}},{{end}}
      {{$arg}}
{{- end}}>;
  std::weak_ptr<Signal{{.Name}}Type> signal_{{.Name}}_;
{{- end}}
{{- end}}`

	propertyDataMembersTmpl = `{{- define "propertyDataMembersTmpl"}}
{{- if .Properties}}
{{/* empty line */}}
{{- range .Properties}}
  {{- $variableName := makePropertyVariableName . | makeVariableName}}
  brillo::dbus_utils::ExportedProperty<{{makePropertyBaseTypeExtract . }}> {{$variableName}}_;
{{- end}}
{{- end}}
{{- end}}`
)

// Generate prints an interface definition and an interface adaptor for each interface in introspects.
func Generate(introspects []introspect.Introspection, useAdaptorMethod bool, f io.Writer, outputFilePath string) error {
	tmpl, err := template.New("adaptor").Funcs(funcMap).Parse(templateText)
	if err != nil {
		return err
	}

	if _, err = tmpl.Parse(interfaceMethodsTmpl); err != nil {
		return err
	}
	if _, err = tmpl.Parse(registerWithDBusObjectTmpl); err != nil {
		return err
	}
	if _, err = tmpl.Parse(sendSignalMethodsTmpl); err != nil {
		return err
	}
	if _, err = tmpl.Parse(propertyMethodImplementationTmpl); err != nil {
		return err
	}
	if _, err = tmpl.Parse(quotedIntrospectionForInterfaceTmpl); err != nil {
		return err
	}
	if _, err = tmpl.Parse(adaptorMethodsTmpl); err != nil {
		return err
	}
	if _, err = tmpl.Parse(signalDataMembersTmpl); err != nil {
		return err
	}
	if _, err = tmpl.Parse(propertyDataMembersTmpl); err != nil {
		return err
	}

	var headerGuard = genutil.GenerateHeaderGuard(outputFilePath)
	return tmpl.Execute(f, templateArgs{introspects, headerGuard, useAdaptorMethod})
}
