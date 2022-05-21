// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "common/fuzzing/proto_to_carbon.h"

#include <string_view>

#include "common/fuzzing/carbon.pb.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"

namespace Carbon {

static auto ExpressionToCarbon(const Fuzzing::Expression& expression,
                               llvm::raw_ostream& out) -> void;
static auto PatternToCarbon(const Fuzzing::Pattern& pattern,
                            llvm::raw_ostream& out) -> void;
static auto StatementToCarbon(const Fuzzing::Statement& statement,
                              llvm::raw_ostream& out) -> void;
static auto DeclarationToCarbon(const Fuzzing::Declaration& declaration,
                                llvm::raw_ostream& out) -> void;

// Produces a valid Carbon identifier, which must match the regex
// `[A-Za-z_][A-Za-z0-9_]*`. In the case when `s` is generated by the
// fuzzing framework, it might contain invalid/non-printable characters.
static auto IdentifierToCarbon(std::string_view s, llvm::raw_ostream& out)
    -> void {
  if (s.empty()) {
    out << "EmptyIdentifier";
  } else {
    if (!llvm::isAlpha(s[0]) && s[0] != '_') {
      // Ensures that identifier starts with a valid character.
      out << 'x';
    }
    for (const char c : s) {
      if (llvm::isAlnum(c) || c == '_') {
        out << c;
      } else {
        out << llvm::toHex(c);
      }
    }
  }
}

static auto StringLiteralToCarbon(std::string_view s, llvm::raw_ostream& out) {
  out << '"';
  out.write_escaped(s, /*UseHexEscapes=*/true);
  out << '"';
}

static auto LibraryNameToCarbon(const Fuzzing::LibraryName& library,
                                llvm::raw_ostream& out) -> void {
  IdentifierToCarbon(library.package_name(), out);

  // Path is optional.
  if (library.has_path()) {
    out << " library ";
    // library.path() is a string literal.
    StringLiteralToCarbon(library.path(), out);
  }
}

static auto PrefixUnaryOperatorToCarbon(std::string_view op,
                                        const Fuzzing::Expression& arg,
                                        llvm::raw_ostream& out) -> void {
  out << op;
  ExpressionToCarbon(arg, out);
}

static auto PostfixUnaryOperatorToCarbon(const Fuzzing::Expression& arg,
                                         std::string_view op,
                                         llvm::raw_ostream& out) -> void {
  ExpressionToCarbon(arg, out);
  out << op;
}

static auto BinaryOperatorToCarbon(const Fuzzing::Expression& lhs,
                                   std::string_view op,
                                   const Fuzzing::Expression& rhs,
                                   llvm::raw_ostream& out) -> void {
  ExpressionToCarbon(lhs, out);
  out << op;
  ExpressionToCarbon(rhs, out);
}

static auto PrimitiveOperatorToCarbon(
    const Fuzzing::PrimitiveOperatorExpression& primitive_operator,
    llvm::raw_ostream& out) -> void {
  const Fuzzing::Expression& arg0 =
      !primitive_operator.arguments().empty()
          ? primitive_operator.arguments(0)
          : Fuzzing::Expression::default_instance();
  const Fuzzing::Expression& arg1 =
      primitive_operator.arguments().size() > 1
          ? primitive_operator.arguments(1)
          : Fuzzing::Expression::default_instance();
  out << "(";
  switch (primitive_operator.op()) {
    case Fuzzing::PrimitiveOperatorExpression::UnknownOperator:
      // `-` is an arbitrary default to avoid getting invalid syntax.
      PrefixUnaryOperatorToCarbon("-", arg0, out);
      break;

    case Fuzzing::PrimitiveOperatorExpression::AddressOf:
      PrefixUnaryOperatorToCarbon("&", arg0, out);
      break;

    case Fuzzing::PrimitiveOperatorExpression::Deref:
      PrefixUnaryOperatorToCarbon("*", arg0, out);
      break;

    case Fuzzing::PrimitiveOperatorExpression::Mul:
      BinaryOperatorToCarbon(arg0, " * ", arg1, out);
      break;

    case Fuzzing::PrimitiveOperatorExpression::Ptr:
      PostfixUnaryOperatorToCarbon(arg0, "*", out);
      break;

    case Fuzzing::PrimitiveOperatorExpression::Neg:
      PrefixUnaryOperatorToCarbon("-", arg0, out);
      break;

    case Fuzzing::PrimitiveOperatorExpression::Sub:
      BinaryOperatorToCarbon(arg0, " - ", arg1, out);
      break;

    case Fuzzing::PrimitiveOperatorExpression::Not:
      // Needs a space to 'unglue' from the operand.
      PrefixUnaryOperatorToCarbon("not ", arg0, out);
      break;

    case Fuzzing::PrimitiveOperatorExpression::Add:
      BinaryOperatorToCarbon(arg0, " + ", arg1, out);
      break;

    case Fuzzing::PrimitiveOperatorExpression::And:
      BinaryOperatorToCarbon(arg0, " and ", arg1, out);
      break;

    case Fuzzing::PrimitiveOperatorExpression::Eq:
      BinaryOperatorToCarbon(arg0, " == ", arg1, out);
      break;

    case Fuzzing::PrimitiveOperatorExpression::Or:
      BinaryOperatorToCarbon(arg0, " or ", arg1, out);
      break;
  }
  out << ")";
}

static auto FieldInitializerToCarbon(const Fuzzing::FieldInitializer& field,
                                     std::string_view separator,
                                     llvm::raw_ostream& out) -> void {
  out << ".";
  IdentifierToCarbon(field.name(), out);
  out << " " << separator << " ";
  ExpressionToCarbon(field.expression(), out);
}

static auto TupleLiteralExpressionToCarbon(
    const Fuzzing::TupleLiteralExpression& tuple_literal,
    llvm::raw_ostream& out) -> void {
  out << "(";
  llvm::ListSeparator sep;
  for (const auto& field : tuple_literal.fields()) {
    out << sep;
    ExpressionToCarbon(field, out);
  }
  if (tuple_literal.fields_size() == 1) {
    // Adding a trailing comma so that generated source will be parsed as a
    // tuple expression. See docs/design/tuples.md.
    out << ", ";
  }
  out << ")";
}

static auto ExpressionToCarbon(const Fuzzing::Expression& expression,
                               llvm::raw_ostream& out) -> void {
  switch (expression.kind_case()) {
    case Fuzzing::Expression::KIND_NOT_SET:
      // Arbitrary default for missing expressions to avoid invalid syntax.
      out << "true";
      break;

    case Fuzzing::Expression::kCall: {
      const auto& call = expression.call();
      ExpressionToCarbon(call.function(), out);
      if (call.argument().kind_case() == Fuzzing::Expression::kTupleLiteral) {
        TupleLiteralExpressionToCarbon(call.argument().tuple_literal(), out);
      } else {
        out << "(";
        ExpressionToCarbon(call.argument(), out);
        out << ")";
      }
      break;
    }

    case Fuzzing::Expression::kFunctionType: {
      const auto& fun_type = expression.function_type();
      out << "__Fn";
      TupleLiteralExpressionToCarbon(fun_type.parameter(), out);
      out << " -> ";
      ExpressionToCarbon(fun_type.return_type(), out);
      break;
    }

    case Fuzzing::Expression::kFieldAccess: {
      const auto& field_access = expression.field_access();
      ExpressionToCarbon(field_access.aggregate(), out);
      out << ".";
      IdentifierToCarbon(field_access.field(), out);
      break;
    }

    case Fuzzing::Expression::kCompoundFieldAccess: {
      const auto& field_access = expression.compound_field_access();
      ExpressionToCarbon(field_access.object(), out);
      out << ".(";
      ExpressionToCarbon(field_access.path(), out);
      out << ")";
      break;
    }

    case Fuzzing::Expression::kIndex: {
      const auto& index = expression.index();
      ExpressionToCarbon(index.aggregate(), out);
      out << "[";
      ExpressionToCarbon(index.offset(), out);
      out << "]";
      break;
    }

    case Fuzzing::Expression::kPrimitiveOperator:
      PrimitiveOperatorToCarbon(expression.primitive_operator(), out);
      break;

    case Fuzzing::Expression::kTupleLiteral: {
      TupleLiteralExpressionToCarbon(expression.tuple_literal(), out);
      break;
    }

    case Fuzzing::Expression::kStructLiteral: {
      const auto& struct_literal = expression.struct_literal();
      out << "{";
      llvm::ListSeparator sep;
      for (const auto& field : struct_literal.fields()) {
        out << sep;
        FieldInitializerToCarbon(field, "=", out);
      }
      out << "}";
      break;
    }

    case Fuzzing::Expression::kStructTypeLiteral: {
      const auto& struct_type_literal = expression.struct_type_literal();
      out << "{";
      llvm::ListSeparator sep;
      for (const auto& field : struct_type_literal.fields()) {
        out << sep;
        FieldInitializerToCarbon(field, ":", out);
      }
      out << "}";
      break;
    }

    case Fuzzing::Expression::kIdentifier: {
      const auto& identifier = expression.identifier();
      IdentifierToCarbon(identifier.name(), out);
      break;
    }

    case Fuzzing::Expression::kIntrinsic: {
      const auto& intrinsic = expression.intrinsic();
      switch (intrinsic.intrinsic()) {
        case Fuzzing::IntrinsicExpression::UnknownIntrinsic:
          // Arbitrary default to avoid getting invalid syntax.
          out << "__intrinsic_print";
          break;

        case Fuzzing::IntrinsicExpression::Print:
          out << "__intrinsic_print";
          break;
      }
      TupleLiteralExpressionToCarbon(intrinsic.argument(), out);
    } break;

    case Fuzzing::Expression::kIfExpression: {
      const auto& if_expression = expression.if_expression();
      out << "if ";
      ExpressionToCarbon(if_expression.condition(), out);
      out << " then ";
      ExpressionToCarbon(if_expression.then_expression(), out);
      out << " else ";
      ExpressionToCarbon(if_expression.else_expression(), out);
      break;
    }

    case Fuzzing::Expression::kBoolTypeLiteral:
      out << "Bool";
      break;

    case Fuzzing::Expression::kBoolLiteral: {
      const auto& bool_literal = expression.bool_literal();
      out << (bool_literal.value() ? "true" : "false");
      break;
    }

    case Fuzzing::Expression::kIntTypeLiteral:
      out << "i32";
      break;

    case Fuzzing::Expression::kIntLiteral: {
      out << expression.int_literal().value();
      break;
    }

    case Fuzzing::Expression::kStringLiteral:
      StringLiteralToCarbon(expression.string_literal().value(), out);
      break;

    case Fuzzing::Expression::kStringTypeLiteral:
      out << "String";
      break;

    case Fuzzing::Expression::kContinuationTypeLiteral:
      out << "__Continuation";
      break;

    case Fuzzing::Expression::kTypeTypeLiteral:
      out << "Type";
      break;

    case Fuzzing::Expression::kUnimplementedExpression:
      // Not really supported.
      // This is an arbitrary default to avoid getting invalid syntax.
      out << "1 __unimplemented_example_infix 2";
      break;

    case Fuzzing::Expression::kArrayTypeLiteral: {
      const Fuzzing::ArrayTypeLiteral& array_literal =
          expression.array_type_literal();
      out << "[";
      ExpressionToCarbon(array_literal.element_type(), out);
      out << "; ";
      ExpressionToCarbon(array_literal.size(), out);
      out << "]";
      break;
    }
  }
}

static auto BindingPatternToCarbon(const Fuzzing::BindingPattern& pattern,
                                   llvm::raw_ostream& out) -> void {
  IdentifierToCarbon(pattern.name(), out);
  out << ": ";
  PatternToCarbon(pattern.type(), out);
}

static auto GenericBindingToCarbon(
    const Fuzzing::GenericBinding& generic_binding, llvm::raw_ostream& out) {
  IdentifierToCarbon(generic_binding.name(), out);
  out << ":! ";
  ExpressionToCarbon(generic_binding.type(), out);
}

static auto TuplePatternToCarbon(const Fuzzing::TuplePattern& tuple_pattern,
                                 llvm::raw_ostream& out) -> void {
  out << "(";
  llvm::ListSeparator sep;
  for (const auto& field : tuple_pattern.fields()) {
    out << sep;
    PatternToCarbon(field, out);
  }
  if (tuple_pattern.fields_size() == 1) {
    // Adding a trailing comma so that generated source will be parsed as a
    // tuple pattern expression. See docs/design/tuples.md.
    out << ", ";
  }
  out << ")";
}

static auto PatternToCarbon(const Fuzzing::Pattern& pattern,
                            llvm::raw_ostream& out) -> void {
  switch (pattern.kind_case()) {
    case Fuzzing::Pattern::KIND_NOT_SET:
      // Arbitrary default to avoid getting invalid syntax.
      out << "auto";
      break;

    case Fuzzing::Pattern::kBindingPattern:
      BindingPatternToCarbon(pattern.binding_pattern(), out);
      break;

    case Fuzzing::Pattern::kTuplePattern:
      TuplePatternToCarbon(pattern.tuple_pattern(), out);
      break;

    case Fuzzing::Pattern::kAlternativePattern: {
      const auto& alternative_pattern = pattern.alternative_pattern();
      ExpressionToCarbon(alternative_pattern.choice_type(), out);
      out << ".";
      IdentifierToCarbon(alternative_pattern.alternative_name(), out);
      TuplePatternToCarbon(alternative_pattern.arguments(), out);
      break;
    }

    // Arbitrary expression.
    case Fuzzing::Pattern::kExpressionPattern: {
      const auto& expression_pattern = pattern.expression_pattern();
      ExpressionToCarbon(expression_pattern.expression(), out);
      break;
    }

    case Fuzzing::Pattern::kAutoPattern:
      out << "auto";
      break;

    case Fuzzing::Pattern::kVarPattern:
      out << "var ";
      PatternToCarbon(pattern.var_pattern().pattern(), out);
      break;

    case Fuzzing::Pattern::kGenericBinding:
      GenericBindingToCarbon(pattern.generic_binding(), out);
      break;
  }
}

static auto BlockStatementToCarbon(const Fuzzing::BlockStatement& block,
                                   llvm::raw_ostream& out) -> void {
  out << "{\n";
  for (const auto& statement : block.statements()) {
    StatementToCarbon(statement, out);
    out << "\n";
  }
  out << "}\n";
}

static auto StatementToCarbon(const Fuzzing::Statement& statement,
                              llvm::raw_ostream& out) -> void {
  switch (statement.kind_case()) {
    case Fuzzing::Statement::KIND_NOT_SET:
      // Arbitrary default to avoid getting invalid syntax.
      out << "true;\n";
      break;

    case Fuzzing::Statement::kExpressionStatement: {
      const auto& expression_statement = statement.expression_statement();
      ExpressionToCarbon(expression_statement.expression(), out);
      out << ";";
      break;
    }

    case Fuzzing::Statement::kAssign: {
      const auto& assign_statement = statement.assign();
      ExpressionToCarbon(assign_statement.lhs(), out);
      out << " = ";
      ExpressionToCarbon(assign_statement.rhs(), out);
      out << ";";
      break;
    }

    case Fuzzing::Statement::kVariableDefinition: {
      const auto& def = statement.variable_definition();
      out << "var ";
      PatternToCarbon(def.pattern(), out);
      out << " = ";
      ExpressionToCarbon(def.init(), out);
      out << ";";
      break;
    }

    case Fuzzing::Statement::kIfStatement: {
      const auto& if_statement = statement.if_statement();
      out << "if (";
      ExpressionToCarbon(if_statement.condition(), out);
      out << ") ";
      BlockStatementToCarbon(if_statement.then_block(), out);
      // `else` is optional.
      if (if_statement.has_else_block()) {
        out << " else ";
        BlockStatementToCarbon(if_statement.else_block(), out);
      }
      break;
    }

    case Fuzzing::Statement::kReturnStatement: {
      const auto& ret = statement.return_statement();
      out << "return";
      if (!ret.is_omitted_expression()) {
        out << " ";
        ExpressionToCarbon(ret.expression(), out);
      }
      out << ";";
      break;
    }

    case Fuzzing::Statement::kBlock:
      BlockStatementToCarbon(statement.block(), out);
      break;

    case Fuzzing::Statement::kWhileStatement: {
      const auto& while_statement = statement.while_statement();
      out << "while (";
      ExpressionToCarbon(while_statement.condition(), out);
      out << ") ";
      BlockStatementToCarbon(while_statement.body(), out);
      break;
    }

    case Fuzzing::Statement::kMatch: {
      const auto& match = statement.match();
      out << "match (";
      ExpressionToCarbon(match.expression(), out);
      out << ") {";
      for (const auto& clause : match.clauses()) {
        if (clause.is_default()) {
          out << "default";
        } else {
          out << "case ";
          PatternToCarbon(clause.pattern(), out);
        }
        out << " => ";
        StatementToCarbon(clause.statement(), out);
      }
      out << "}";
      break;
    }

    case Fuzzing::Statement::kContinuation: {
      const auto& continuation = statement.continuation();
      out << "__continuation ";
      IdentifierToCarbon(continuation.name(), out);
      BlockStatementToCarbon(continuation.body(), out);
      break;
    }

    case Fuzzing::Statement::kRun: {
      const auto& run = statement.run();
      out << "__run ";
      ExpressionToCarbon(run.argument(), out);
      out << ";";
      break;
    }

    case Fuzzing::Statement::kAwaitStatement:
      out << "__await;";
      break;

    case Fuzzing::Statement::kBreakStatement:
      out << "break;";
      break;

    case Fuzzing::Statement::kContinueStatement:
      out << "continue;";
      break;
  }
}

static auto ReturnTermToCarbon(const Fuzzing::ReturnTerm& return_term,
                               llvm::raw_ostream& out) -> void {
  switch (return_term.kind()) {
    case Fuzzing::ReturnTerm::UnknownReturnKind:
    case Fuzzing::ReturnTerm::Omitted:
      break;
    case Fuzzing::ReturnTerm::Auto:
      out << " -> auto";
      break;
    case Fuzzing::ReturnTerm::Expression:
      out << " -> ";
      ExpressionToCarbon(return_term.type(), out);
      break;
  }
}

static auto DeclarationToCarbon(const Fuzzing::Declaration& declaration,
                                llvm::raw_ostream& out) -> void {
  switch (declaration.kind_case()) {
    case Fuzzing::Declaration::KIND_NOT_SET:
      // Arbitrary default to avoid getting invalid syntax.
      out << "var x: i32;";
      break;

    case Fuzzing::Declaration::kFunction: {
      const auto& function = declaration.function();
      out << "fn ";
      IdentifierToCarbon(function.name(), out);

      if (!function.deduced_parameters().empty() || function.has_me_pattern()) {
        out << "[";
        llvm::ListSeparator sep;
        for (const Fuzzing::GenericBinding& p : function.deduced_parameters()) {
          out << sep;
          GenericBindingToCarbon(p, out);
        }
        if (function.has_me_pattern()) {
          // This is a class method.
          out << sep;
          BindingPatternToCarbon(function.me_pattern(), out);
        }
        out << "]";
      }
      TuplePatternToCarbon(function.param_pattern(), out);
      ReturnTermToCarbon(function.return_term(), out);

      // Body is optional.
      if (function.has_body()) {
        out << "\n";
        BlockStatementToCarbon(function.body(), out);
      } else {
        out << ";";
      }
      break;
    }

    case Fuzzing::Declaration::kClassDeclaration: {
      const auto& class_declaration = declaration.class_declaration();
      out << "class ";
      IdentifierToCarbon(class_declaration.name(), out);

      // type_params is optional.
      if (class_declaration.has_type_params()) {
        TuplePatternToCarbon(class_declaration.type_params(), out);
      }

      out << "{\n";
      for (const auto& member : class_declaration.members()) {
        DeclarationToCarbon(member, out);
        out << "\n";
      }
      out << "}";
      break;
    }

    case Fuzzing::Declaration::kChoice: {
      const auto& choice = declaration.choice();
      out << "choice ";
      IdentifierToCarbon(choice.name(), out);

      out << "{";
      llvm::ListSeparator sep;
      for (const auto& alternative : choice.alternatives()) {
        out << sep;
        IdentifierToCarbon(alternative.name(), out);
        ExpressionToCarbon(alternative.signature(), out);
      }
      out << "}";
      break;
    }

    case Fuzzing::Declaration::kVariable: {
      const auto& var = declaration.variable();
      out << "var ";
      BindingPatternToCarbon(var.binding(), out);

      // Initializer is optional.
      if (var.has_initializer()) {
        out << " = ";
        ExpressionToCarbon(var.initializer(), out);
      }
      out << ";";
      break;
    }

    case Fuzzing::Declaration::kInterface: {
      const auto& interface = declaration.interface();
      out << "interface ";
      IdentifierToCarbon(interface.name(), out);
      out << " {\n";
      for (const auto& member : interface.members()) {
        DeclarationToCarbon(member, out);
        out << "\n";
      }
      out << "}";
      // TODO: need to handle interface.self()?
      break;
    }

    case Fuzzing::Declaration::kImpl: {
      const auto& impl = declaration.impl();
      if (impl.kind() == Fuzzing::ImplDeclaration::ExternalImpl) {
        out << "external ";
      }
      out << "impl ";
      ExpressionToCarbon(impl.impl_type(), out);
      out << " as ";
      ExpressionToCarbon(impl.interface(), out);
      out << " {\n";
      for (const auto& member : impl.members()) {
        DeclarationToCarbon(member, out);
        out << "\n";
      }
      out << "}";
      break;
    }

    case Fuzzing::Declaration::kAlias: {
      const auto& alias = declaration.alias();
      out << "alias ";
      IdentifierToCarbon(alias.name(), out);
      out << " = ";
      ExpressionToCarbon(alias.target(), out);
      out << ";";
      break;
    }
  }
}

static auto ProtoToCarbon(const Fuzzing::CompilationUnit& compilation_unit,
                          llvm::raw_ostream& out) -> void {
  out << "// Generated by proto_to_carbon.\n\n";
  out << "package ";
  LibraryNameToCarbon(compilation_unit.package_statement(), out);
  out << (compilation_unit.is_api() ? " api" : " impl") << ";\n";

  if (!compilation_unit.declarations().empty()) {
    out << "\n";
    for (const auto& declaration : compilation_unit.declarations()) {
      DeclarationToCarbon(declaration, out);
      out << "\n";
    }
  }
}

auto ProtoToCarbon(const Fuzzing::CompilationUnit& compilation_unit)
    -> std::string {
  std::string source;
  llvm::raw_string_ostream out(source);
  ProtoToCarbon(compilation_unit, out);
  return source;
}

}  // namespace Carbon
