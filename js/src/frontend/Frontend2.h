/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef frontend_Frontend2_h
#define frontend_Frontend2_h

#include "js/CompilationAndEvaluation.h"
#include "js/SourceText.h"
#include "vm/GlobalObject.h"
#include "vm/JSAtom.h"
#include "vm/JSContext.h"
#include "vm/JSFunction.h"
#include "vm/JSScript.h"

bool InitScript(JSContext* cx, JS::HandleScript script,
                JS::HandleFunction functionProto);
bool Create(JSContext* cx, const uint8_t* bytes, size_t length);

#endif /* frontend_Frontend2_h */
