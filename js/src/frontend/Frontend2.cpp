/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "frontend/Frontend2.h"

#include "mozilla/Span.h"  // mozilla::{Span, MakeSpan}

#include <stddef.h>  // size_t
#include <stdint.h>  // uint8_t, uint32_t

#include "jsapi.h"

#include "frontend-rs/frontend-rs.h"  // CVec, JsparagusResult, JsparagusCompileOptions, free_jsparagus, run_jsparagus
#include "frontend/CompilationInfo.h"      // CompilationInfo
#include "frontend/SourceNotes.h"          // jssrcnote
#include "gc/Rooting.h"                    // RootedScriptSourceObject
#include "js/HeapAPI.h"                    // JS::GCCellPtr
#include "js/RootingAPI.h"                 // JS::Handle
#include "js/TypeDecls.h"                  // Rooted{Script,Value,String,Object}
#include "vm/JSAtom.h"                     // AtomizeUTF8Chars
#include "vm/JSScript.h"                   // JSScript

#include "vm/JSContext-inl.h"  // AutoKeepAtoms (used by BytecodeCompiler)

using mozilla::Utf8Unit;

using namespace js::gc;
using namespace js::frontend;
using namespace js;

namespace js {

namespace frontend {

class SmooshScriptStencil : public ScriptStencil {
  const JsparagusResult& jsparagus_;

  void init() {
    lineno = 1;
    column = 0;

    natoms = jsparagus_.strings.len;

    ngcthings = 1;

    numResumeOffsets = 0;
    numScopeNotes = 0;
    numTryNotes = 0;

    mainOffset = 0;
    nfixed = 0;
    nslots = nfixed + jsparagus_.maximum_stack_depth;
    bodyScopeIndex = 0;
    numICEntries = jsparagus_.num_ic_entries;
    numBytecodeTypeSets = 0;

    strict = false;
    bindingsAccessedDynamically = false;
    hasCallSiteObj = false;
    isForEval = false;
    isModule = false;
    isFunction = false;
    hasNonSyntacticScope = false;
    needsFunctionEnvironmentObjects = false;
    hasModuleGoal = false;

    code = mozilla::MakeSpan(jsparagus_.bytecode.data, jsparagus_.bytecode.len);
    MOZ_ASSERT(notes.IsEmpty());
  }

 public:
  explicit SmooshScriptStencil(const JsparagusResult& jsparagus)
      : jsparagus_(jsparagus) {
    init();
  }

  virtual bool finishGCThings(JSContext* cx,
                              mozilla::Span<JS::GCCellPtr> gcthings) const {
    gcthings[0] = JS::GCCellPtr(&cx->global()->emptyGlobalScope());
    return true;
  }

  virtual bool initAtomMap(JSContext* cx, GCPtrAtom* atoms) const {
    for (uint32_t i = 0; i < natoms; i++) {
      const CVec<uint8_t>& string = jsparagus_.strings.data[i];
      JSAtom* atom = AtomizeUTF8Chars(cx, (const char*)string.data, string.len);
      if (!atom) {
        return false;
      }
      atoms[i] = atom;
    }

    return true;
  }

  virtual void finishResumeOffsets(
      mozilla::Span<uint32_t> resumeOffsets) const {}

  virtual void finishScopeNotes(mozilla::Span<ScopeNote> scopeNotes) const {}

  virtual void finishTryNotes(mozilla::Span<JSTryNote> tryNotes) const {}

  virtual void finishInnerFunctions() const {}
};

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

void ReportVisageCompileError(JSContext* cx, ErrorMetadata&& metadata,
                              int errorNumber, ...) {
  va_list args;
  va_start(args, errorNumber);
  ReportCompileErrorUTF8(cx, std::move(metadata), /* notes = */ nullptr,
                         JSREPORT_ERROR, errorNumber, &args);
  va_end(args);
}

/* static */
JSScript* Jsparagus::compileGlobalScript(CompilationInfo& compilationInfo,
                                         JS::SourceText<Utf8Unit>& srcBuf,
                                         bool* unimplemented) {
  // FIXME: check info members and return with *unimplemented = true
  //        if any field doesn't match to run_jsparagus.

  auto bytes = reinterpret_cast<const uint8_t*>(srcBuf.get());
  size_t length = srcBuf.length();

  JSContext* cx = compilationInfo.cx;

  const auto& options = compilationInfo.options;
  JsparagusCompileOptions compileOptions;
  compileOptions.no_script_rval = options.noScriptRval;

  JsparagusResult jsparagus = run_jsparagus(bytes, length, &compileOptions);
  AutoFreeJsparagusResult afjr(&jsparagus);

  if (jsparagus.error.data) {
    *unimplemented = false;
    ErrorMetadata metadata;
    metadata.filename = "<unknown>";
    metadata.lineNumber = 1;
    metadata.columnNumber = 0;
    metadata.isMuted = false;
    ReportVisageCompileError(
        cx, std::move(metadata), JSMSG_VISAGE_COMPILE_ERROR,
        reinterpret_cast<const char*>(jsparagus.error.data));
    return nullptr;
  }

  if (jsparagus.unimplemented) {
    *unimplemented = true;
    return nullptr;
  }

  *unimplemented = false;

  RootedScriptSourceObject sso(cx,
                               frontend::CreateScriptSourceObject(cx, options));
  if (!sso) {
    return nullptr;
  }

  RootedObject proto(cx);
  if (!GetFunctionPrototype(cx, GeneratorKind::NotGenerator,
                            FunctionAsyncKind::SyncFunction, &proto)) {
    return nullptr;
  }

  RootedScript script(cx, JSScript::Create(cx, cx->global(), options, sso, 0,
                                           length, 0, length, 1, 0));

  SmooshScriptStencil stencil(jsparagus);
  if (!JSScript::fullyInitFromStencil(cx, script, stencil)) {
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

bool RustParseScript(JSContext* cx, const uint8_t* bytes, size_t length) {
  if (test_parse_script(bytes, length)) {
    return true;
  }
  JS_ReportErrorASCII(cx, "Rust parse script failed");
  return false;
}

bool RustParseModule(JSContext* cx, const uint8_t* bytes, size_t length) {
  if (test_parse_module(bytes, length)) {
    return true;
  }
  JS_ReportErrorASCII(cx, "Rust parse module failed");
  return false;
}

}  // namespace frontend

}  // namespace js
