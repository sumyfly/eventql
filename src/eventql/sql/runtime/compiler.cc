/**
 * Copyright (c) 2016 DeepCortex GmbH <legal@eventql.io>
 * Authors:
 *   - Paul Asmuth <paul@eventql.io>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License ("the license") as
 * published by the Free Software Foundation, either version 3 of the License,
 * or any later version.
 *
 * In accordance with Section 7(e) of the license, the licensing of the Program
 * under the license does not imply a trademark license. Therefore any rights,
 * title and interest in our trademarks remain entirely with us.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the license for more details.
 *
 * You can be released from the requirements of the license by purchasing a
 * commercial license. Buying such a license is mandatory as soon as you develop
 * commercial activities involving this program without disclosing the source
 * code of your own applications
 */
#include <stdlib.h>
#include <eventql/util/RegExp.h>
#include <eventql/sql/parser/astnode.h>
#include <eventql/sql/parser/token.h>
#include <eventql/sql/runtime/compiler.h>
#include <eventql/sql/runtime/symboltable.h>
#include <eventql/sql/runtime/LikePattern.h>
#include <eventql/sql/svalue.h>

namespace csql {

ScopedPtr<VM::Program> Compiler::compile(
    Transaction* ctx,
    RefPtr<ValueExpressionNode> node,
    SymbolTable* symbol_table) {
  ScratchMemory static_storage;
  size_t dynamic_storage_size = 0;

  auto expr = compileValueExpression(
      node,
      &dynamic_storage_size,
      &static_storage,
      symbol_table);

  return mkScoped(
      new VM::Program(
          ctx,
          expr,
          std::move(static_storage),
          dynamic_storage_size));
}

VM::Instruction* Compiler::compileValueExpression(
   RefPtr<ValueExpressionNode> node,
   size_t* dynamic_storage_size,
   ScratchMemory* static_storage,
   SymbolTable* symbol_table) {
  if (dynamic_cast<ColumnReferenceNode*>(node.get())) {
    return compileColumnReference(
        node.asInstanceOf<ColumnReferenceNode>(),
        static_storage,
        symbol_table);
  }

  if (dynamic_cast<LiteralExpressionNode*>(node.get())) {
    return compileLiteral(
        node.asInstanceOf<LiteralExpressionNode>(),
        dynamic_storage_size,
        static_storage,
        symbol_table);
  }

  if (dynamic_cast<IfExpressionNode*>(node.get())) {
    return compileIfStatement(
        node.asInstanceOf<IfExpressionNode>(),
        dynamic_storage_size,
        static_storage,
        symbol_table);
  }

  if (dynamic_cast<CallExpressionNode*>(node.get())) {
    return compileMethodCall(
        node.asInstanceOf<CallExpressionNode>(),
        dynamic_storage_size,
        static_storage,
        symbol_table);
  }

  if (dynamic_cast<RegexExpressionNode*>(node.get())) {
    return compileRegexOperator(
        node.asInstanceOf<RegexExpressionNode>(),
        dynamic_storage_size,
        static_storage,
        symbol_table);
  }

  if (dynamic_cast<LikeExpressionNode*>(node.get())) {
    return compileLikeOperator(
        node.asInstanceOf<LikeExpressionNode>(),
        dynamic_storage_size,
        static_storage,
        symbol_table);
  }

  RAISE(kRuntimeError, "internal error: can't compile expression");
}

VM::Instruction* Compiler::compileLiteral(
    RefPtr<LiteralExpressionNode> node,
    size_t* dynamic_storage_size,
    ScratchMemory* static_storage,
   SymbolTable* symbol_table) {
  auto ins = static_storage->construct<VM::Instruction>();
  ins->type = VM::X_LITERAL;
  ins->arg0 = static_storage->construct<SValue>(node->value());
  ins->child = nullptr;
  ins->next  = nullptr;

  return ins;
}

VM::Instruction* Compiler::compileColumnReference(
    RefPtr<ColumnReferenceNode> node,
    ScratchMemory* static_storage,
   SymbolTable* symbol_table) {
  auto col_idx = node->columnIndex();

  if (col_idx == size_t(-1)) {
    auto ins = static_storage->construct<VM::Instruction>();
    ins->type = VM::X_LITERAL;
    ins->arg0 = static_storage->construct<SValue>();
    ins->child = nullptr;
    ins->next  = nullptr;
    return ins;
  } else {
    auto ins = static_storage->construct<VM::Instruction>();
    ins->type = VM::X_INPUT;
    ins->arg0 = (void *) col_idx;
    ins->argn = 0;
    ins->child = nullptr;
    ins->next  = nullptr;
    return ins;
  }
}

VM::Instruction* Compiler::compileMethodCall(
    RefPtr<CallExpressionNode> node,
    size_t* dynamic_storage_size,
    ScratchMemory* static_storage,
   SymbolTable* symbol_table) {
  auto symbol = symbol_table->lookup(node->symbol());
  const auto& args = node->arguments();

  auto op = static_storage->construct<VM::Instruction>();
  op->arg0  = nullptr;
  op->argn  = args.size();
  op->child = nullptr;
  op->next  = nullptr;

  switch (symbol.type) {
    case FN_PURE:
      op->type = VM::X_CALL_PURE;
      op->vtable.t_pure = symbol.vtable.t_pure;
      break;
    case FN_AGGREGATE:
      op->type = VM::X_CALL_AGGREGATE;
      op->arg0 = (void *) *dynamic_storage_size;
      op->vtable.t_aggregate = symbol.vtable.t_aggregate;
      *dynamic_storage_size += symbol.vtable.t_aggregate.scratch_size;
      break;
  }

  auto cur = &op->child;
  for (auto e : args) {
    auto next = compileValueExpression(
        e,
        dynamic_storage_size,
        static_storage,
        symbol_table);

    *cur = next;
    cur = &next->next;
  }

  return op;
}

VM::Instruction* Compiler::compileIfStatement(
    RefPtr<IfExpressionNode> node,
    size_t* dynamic_storage_size,
    ScratchMemory* static_storage,
   SymbolTable* symbol_table) {
  const auto& args = node->arguments();

  auto op = static_storage->construct<VM::Instruction>();
  op->type = VM::X_IF;
  op->arg0  = nullptr;
  op->argn  = args.size();
  op->child = nullptr;
  op->next  = nullptr;

  auto cur = &op->child;
  for (auto e : args) {
    auto next = compileValueExpression(
        e,
        dynamic_storage_size,
        static_storage,
        symbol_table);

    *cur = next;
    cur = &next->next;
  }

  return op;
}

VM::Instruction* Compiler::compileRegexOperator(
    RefPtr<RegexExpressionNode> node,
    size_t* dynamic_storage_size,
    ScratchMemory* static_storage,
   SymbolTable* symbol_table) {
  auto ins = static_storage->construct<VM::Instruction>();
  ins->type = VM::X_REGEX;
  ins->arg0 = static_storage->construct<RegExp>(node->pattern());
  ins->next  = nullptr;
  ins->child = compileValueExpression(
      node->subject(),
      dynamic_storage_size,
      static_storage,
      symbol_table);

  return ins;
}

VM::Instruction* Compiler::compileLikeOperator(
    RefPtr<LikeExpressionNode> node,
    size_t* dynamic_storage_size,
    ScratchMemory* static_storage,
   SymbolTable* symbol_table) {
  auto ins = static_storage->construct<VM::Instruction>();
  ins->type = VM::X_LIKE;
  ins->arg0 = static_storage->construct<LikePattern>(node->pattern());
  ins->next  = nullptr;
  ins->child = compileValueExpression(
      node->subject(),
      dynamic_storage_size,
      static_storage,
      symbol_table);

  return ins;
}

}
