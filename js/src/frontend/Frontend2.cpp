/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "frontend/Frontend2.h"

#include "gc/AllocKind.h"

using JS::RootedScript;
using mozilla::Utf8Unit;

using namespace js::gc;
using namespace js::frontend;
using namespace js;

bool InitScript(JSContext* cx, HandleScript script,
                HandleFunction canoicalFunction) {
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

  uint32_t codeLength = 1;
  uint32_t noteLength = 2;
  uint32_t numResumeOffsets = 0;
  uint32_t numScopeNotes = 0;
  uint32_t numTryNotes = 0;
  if (!script->createImmutableScriptData(cx, codeLength, noteLength,
                                         numResumeOffsets, numScopeNotes,
                                         numTryNotes)) {
    return false;
  }

  jsbytecode* code = script->immutableScriptData()->code();
  code[0] = JSOP_RETRVAL;

  jssrcnote* notes = script->immutableScriptData()->notes();
  notes[0] = SRC_NULL;
  notes[1] = SRC_NULL;

  return script->shareScriptData(cx);
}

bool Create(JSContext* cx, const char* bytes, size_t length) {
  JS::CompileOptions options(cx);
  options.setIntroductionType("js shell interactive")
      .setIsRunOnce(true)
      .setFileAndLine("typein", 1);

  JS::SourceText<Utf8Unit> srcBuf;
  if (!srcBuf.init(cx, bytes, length, JS::SourceOwnership::Borrowed)) {
    return false;
  }

  ScriptSource* ss = cx->new_<ScriptSource>();
  if (!ss) {
    return false;
  }

  ScriptSourceHolder ssHolder(ss);

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

  if (!InitScript(cx, script, nullptr)) {
    return false;
  }

  RootedValue result(cx);
  if (!JS_ExecuteScript(cx, script, &result)) {
    return false;
  }

  return true;
}
