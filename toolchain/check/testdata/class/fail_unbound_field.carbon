// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// AUTOUPDATE

class Class {
  var field: i32;
  fn F() -> i32 {
    // CHECK:STDERR: fail_unbound_field.carbon:[[@LINE+3]]:12: ERROR: Expression cannot be used as a value.
    // CHECK:STDERR:     return field;
    // CHECK:STDERR:            ^~~~~
    return field;
  }
}

fn G() -> i32 {
  // CHECK:STDERR: fail_unbound_field.carbon:[[@LINE+3]]:10: ERROR: Expression cannot be used as a value.
  // CHECK:STDERR:   return Class.field;
  // CHECK:STDERR:          ^~~~~~~~~~~
  return Class.field;
}

// CHECK:STDOUT: --- fail_unbound_field.carbon
// CHECK:STDOUT:
// CHECK:STDOUT: constants {
// CHECK:STDOUT:   %.1: type = struct_type {.field: i32} [template]
// CHECK:STDOUT: }
// CHECK:STDOUT:
// CHECK:STDOUT: file {
// CHECK:STDOUT:   package: <namespace> = namespace package, {.Class = %Class.decl, .G = %G}
// CHECK:STDOUT:   %Class.decl = class_decl @Class, ()
// CHECK:STDOUT:   %Class: type = class_type @Class [template]
// CHECK:STDOUT:   %G: <function> = fn_decl @G [template]
// CHECK:STDOUT: }
// CHECK:STDOUT:
// CHECK:STDOUT: class @Class {
// CHECK:STDOUT:   %.loc8_12.1: type = unbound_element_type Class, i32 [template]
// CHECK:STDOUT:   %.loc8_12.2: <unbound element of class Class> = field_decl field, element0 [template]
// CHECK:STDOUT:   %field: <unbound element of class Class> = bind_name field, %.loc8_12.2 [template = %.loc8_12.2]
// CHECK:STDOUT:   %F: <function> = fn_decl @F [template]
// CHECK:STDOUT:
// CHECK:STDOUT: !members:
// CHECK:STDOUT:   .field = %field
// CHECK:STDOUT:   .F = %F
// CHECK:STDOUT: }
// CHECK:STDOUT:
// CHECK:STDOUT: fn @F() -> i32 {
// CHECK:STDOUT: !entry:
// CHECK:STDOUT:   %field.ref: <unbound element of class Class> = name_ref field, @Class.%field [template = @Class.%.loc8_12.2]
// CHECK:STDOUT:   return <error>
// CHECK:STDOUT: }
// CHECK:STDOUT:
// CHECK:STDOUT: fn @G() -> i32 {
// CHECK:STDOUT: !entry:
// CHECK:STDOUT:   %Class.ref: type = name_ref Class, file.%Class [template = file.%Class]
// CHECK:STDOUT:   %field.ref: <unbound element of class Class> = name_ref field, @Class.%field [template = @Class.%.loc8_12.2]
// CHECK:STDOUT:   return <error>
// CHECK:STDOUT: }
// CHECK:STDOUT:
