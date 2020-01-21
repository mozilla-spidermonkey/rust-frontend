/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "frontend/Frontend2.h"

#include "mozilla/ScopeExit.h"
#include "mozilla/Span.h"  // mozilla::Span

#include <stddef.h>  // size_t
#include <stdint.h>  // uint8_t, uint32_t

#include "jsapi.h"

#include "frontend-rs/frontend-rs.h"  // CVec, JsparagusResult, free_jsparagus, run_jsparagus
#include "frontend/BytecodeCompilation.h"  // GlobalScriptInfo
#include "frontend/SourceNotes.h"          // jssrcnote
#include "gc/Rooting.h"                    // RootedScriptSourceObject
#include "js/HeapAPI.h"                    // JS::GCCellPtr
#include "js/RootingAPI.h"                 // JS::Handle
#include "js/TypeDecls.h"  // Rooted{Script,Value,String,Object}, jsbytecode
#include "vm/JSAtom.h"     // AtomizeUTF8Chars
#include "vm/JSScript.h"   // JSScript

#include "vm/JSContext-inl.h"  // AutoKeepAtoms (used by BytecodeCompiler)

using mozilla::Utf8Unit;

using namespace js::gc;
using namespace js::frontend;
using namespace js;

// https://stackoverflow.com/questions/3407012/c-rounding-up-to-the-nearest-multiple-of-a-number
int roundUp(int numToRound, int multiple) {
  // assert(multiple && ((multiple & (multiple - 1)) == 0));
  return (numToRound + multiple - 1) & -multiple;
}

namespace js {

namespace frontend {

/* static */
bool Jsparagus::initScript(JSContext* cx, JS::Handle<JSScript*> script,
                           const JsparagusResult& jsparagus) {
  uint32_t numGCThings = 1;
  if (!JSScript::createPrivateScriptData(cx, script, numGCThings)) {
    return false;
  }

  mozilla::Span<JS::GCCellPtr> gcthings = script->data_->gcthings();
  gcthings[0] = JS::GCCellPtr(&cx->global()->emptyGlobalScope());

  uint32_t natoms = jsparagus.strings.len;
  if (!script->createScriptData(cx, natoms)) {
    return false;
  }
  for (uint32_t i = 0; i < natoms; i++) {
    const CVec<uint8_t>& string = jsparagus.strings.data[i];
    script->getAtom(i) =
        AtomizeUTF8Chars(cx, (const char*)string.data, string.len);
  }

  uint32_t codeLength = jsparagus.bytecode.len;
  uint32_t noteLength =
      roundUp(1 + jsparagus.bytecode.len, 4) - (1 + jsparagus.bytecode.len);
  uint32_t numResumeOffsets = 0;
  uint32_t numScopeNotes = 0;
  uint32_t numTryNotes = 0;
  if (!script->createImmutableScriptData(cx, codeLength, noteLength,
                                         numResumeOffsets, numScopeNotes,
                                         numTryNotes)) {
    return false;
  }
  js::ImmutableScriptData* data = script->immutableScriptData();

  // Initialize POD fields
  data->mainOffset = 0;
  data->nfixed = 0;
  data->nslots = data->nfixed + jsparagus.maximum_stack_depth;
  data->bodyScopeIndex = 0;
  data->numICEntries = jsparagus.num_ic_entries;
  data->numBytecodeTypeSets = 0;

  // Initialize trailing arrays
  std::copy_n(jsparagus.bytecode.data, codeLength, data->code());
  std::fill_n(data->notes(), noteLength, SRC_NULL);

  return script->shareScriptData(cx);
}

// Free given JsparagusResult on leaving scope.
class AutoFreeJsparagusResult {
  JsparagusResult* result_;

 public:
  AutoFreeJsparagusResult() = delete;

  AutoFreeJsparagusResult(JsparagusResult* result) : result_(result) {}
  ~AutoFreeJsparagusResult() {
    if (result_) {
      free_jsparagus(*result_);
    }
  }
};

void ReportVisageCompileError(JSContext* cx, ErrorMetadata&& metadata, int errorNumber, ...) {
  va_list args;
  va_start(args, errorNumber);
  ReportCompileError(cx, std::move(metadata), /* notes = */ nullptr, JSREPORT_ERROR,
                     errorNumber, &args);
  va_end(args);
}


/* static */
JSScript* Jsparagus::compileGlobalScript(GlobalScriptInfo& info,
                                         JS::SourceText<Utf8Unit>& srcBuf,
                                         bool* unimplemented) {
  // FIXME: check info members and return with *unimplemented = true
  //        if any field doesn't match to run_jsparagus.

  auto bytes = reinterpret_cast<const uint8_t*>(srcBuf.get());
  size_t length = srcBuf.length();

  JSContext* cx = info.context();
  JsparagusResult jsparagus = run_jsparagus(bytes, length);
  AutoFreeJsparagusResult afjr(&jsparagus);

  if (jsparagus.error.data) {
    *unimplemented = false;
    ErrorMetadata metadata;
    metadata.filename = "<unknown>";
    metadata.lineNumber = 1;
    metadata.columnNumber = 0;
    metadata.isMuted = false;
    ReportVisageCompileError(cx, std::move(metadata), JSMSG_VISAGE_COMPILE_ERROR,
                             reinterpret_cast<const char*>(jsparagus.error.data));
    return nullptr;
  }

  if (jsparagus.unimplemented) {
    *unimplemented = true;
    return nullptr;
  }

  *unimplemented = false;

  ScriptSource* ss = cx->new_<ScriptSource>();
  if (!ss) {
    return nullptr;
  }

  RootedScriptSourceObject sso(cx, frontend::CreateScriptSourceObject(cx, info.getOptions()));
  if (!sso) {
    return nullptr;
  }

  RootedObject proto(cx);
  if (!GetFunctionPrototype(cx, GeneratorKind::NotGenerator,
                            FunctionAsyncKind::SyncFunction, &proto)) {
    return nullptr;
  }

  RootedScript script(cx, JSScript::Create(cx, cx->global(), info.getOptions(),
                                           sso, 0, length, 0, length, 1, 0));

  if (!initScript(cx, script, jsparagus)) {
    return nullptr;
  }

#if defined(DEBUG) || defined(JS_JITSPEW)
  Sprinter sprinter(cx);
  if (!sprinter.init()) {
    return nullptr;
  }
  if (!Disassemble(cx, script, true, &sprinter, DisassembleSkeptically::Yes)) {
    return nullptr;
  }
  printf("%s\n", sprinter.string());
  if (!Disassemble(cx, script, true, &sprinter, DisassembleSkeptically::No)) {
    return nullptr;
  }
  // (don't bother printing it)
#endif

  return script;
}

bool RustParseScript(JSContext* cx, const uint8_t* bytes, size_t length)
{
  if (test_parse_script(bytes, length)) {
    return true;
  }
  JS_ReportErrorASCII(cx, "Rust parse script failed");
  return false;
}

bool RustParseModule(JSContext* cx, const uint8_t* bytes, size_t length)
{
  if (test_parse_module(bytes, length)) {
    return true;
  }
  JS_ReportErrorASCII(cx, "Rust parse module failed");
  return false;
}

}  // namespace frontend

}  // namespace js
