/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "frontend/Frontend2.h"

// #include "frontend-rs/frontend-rs.h"
#include "gc/AllocKind.h"

using JS::RootedScript;
using mozilla::Utf8Unit;

using namespace js::gc;
using namespace js::frontend;
using namespace js;

// https://stackoverflow.com/questions/3407012/c-rounding-up-to-the-nearest-multiple-of-a-number
int roundUp(int numToRound, int multiple) {
  // assert(multiple && ((multiple & (multiple - 1)) == 0));
  return (numToRound + multiple - 1) & -multiple;
}

bool InitScript(JSContext* cx, HandleScript script,
                HandleFunction canoicalFunction, const Bytecode& bytecode) {
  uint32_t natoms = 0;
  if (!script->createScriptData(cx, natoms)) {
    return false;
  }

  uint32_t numGCThings = 1;
  if (!JSScript::createPrivateScriptData(cx, script, numGCThings)) {
    return false;
  }

  RootedScope enclosing(cx, &cx->global()->emptyGlobalScope());
  Scope* functionProtoScope = FunctionScope::create(
      cx, nullptr, false, false, canoicalFunction, enclosing);
  if (!functionProtoScope) {
    return false;
  }

  mozilla::Span<JS::GCCellPtr> gcthings = script->data_->gcthings();
  gcthings[0] = JS::GCCellPtr(functionProtoScope);

  uint32_t codeLength = bytecode.len;
  uint32_t noteLength = roundUp(1 + bytecode.len, 4) - (1 + bytecode.len);
  uint32_t numResumeOffsets = 0;
  uint32_t numScopeNotes = 0;
  uint32_t numTryNotes = 0;
  if (!script->createImmutableScriptData(cx, codeLength, noteLength,
                                         numResumeOffsets, numScopeNotes,
                                         numTryNotes)) {
    return false;
  }

  jsbytecode* code = script->immutableScriptData()->code();
  for (size_t i = 0; i < codeLength; i++) {
    code[i] = bytecode.data[i];
  }

  jssrcnote* notes = script->immutableScriptData()->notes();
  for (size_t i = 0; i < noteLength; i++) {
    notes[i] = SRC_NULL;
  }

  return script->shareScriptData(cx);
}

static bool Execute(JSContext* cx, HandleScript script) {
  RootedValue result(cx);
  result.setUndefined();
  if (!JS_ExecuteScript(cx, script, &result)) {
    return false;
  }

  if (!result.isUndefined()) {
    // Print.
    RootedString str(cx, JS_ValueToSource(cx, result));
    if (!str) {
      return false;
    }

    UniqueChars utf8chars = JS_EncodeStringToUTF8(cx, str);
    if (!utf8chars) {
      return false;
    }
    printf("%s\n", utf8chars.get());
  }
  return true;
}

bool Create(JSContext* cx, const uint8_t* bytes, size_t length) {
  // void free_bytecode(Bytecode bytecode);

  // Bytecode run_jsparagus(const uint8_t *text, uintptr_t text_len);

  Bytecode bytecode = run_jsparagus(bytes, length);

  JS::CompileOptions options(cx);
  options.setIntroductionType("js shell interactive")
      .setIsRunOnce(true)
      .setFileAndLine("typein", 1);

  // JS::SourceText<Utf8Unit> srcBuf;
  // if (!srcBuf.init(cx, (const char*)bytes, length,
  //                  JS::SourceOwnership::Borrowed)) {
  //   return false;
  // }

  ScriptSource* ss = cx->new_<ScriptSource>();
  if (!ss) {
    return false;
  }

  ScriptSourceHolder ssHolder(ss);  // TODO

  if (!ss->initFromOptions(cx, options, mozilla::Nothing())) {
    return false;
  }

  RootedScriptSourceObject sso(cx, ScriptSourceObject::create(cx, ss));
  if (!sso) {
    return false;
  }

  if (!ScriptSourceObject::initFromOptions(cx, sso, options)) {
    return false;
  }

  RootedObject proto(cx);
  if (!GetFunctionPrototype(cx, GeneratorKind::NotGenerator,
                            FunctionAsyncKind::SyncFunction, &proto)) {
    return false;
  }

  Rooted<JSAtom*> name(cx, cx->names().name);
  FunctionFlags flags;
  flags.setInterpreted();
  RootedFunction canoicalFunction(
      cx, NewFunctionWithProto(cx, nullptr, 0, flags, nullptr, name, proto,
                               AllocKind::FUNCTION, TenuredObject));

  RootedScript script(cx,
                      JSScript::Create(cx, options, sso, 0, length, 0, length));

  if (!InitScript(cx, script, canoicalFunction, bytecode)) {
    return false;
  }

  if (!Execute(cx, script)) {
    return false;
  }

  free_bytecode(bytecode);

  return true;
}
