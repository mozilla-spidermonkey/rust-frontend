#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <new>

template<typename T>
struct CVec {
  T *data;
  uintptr_t len;
  uintptr_t capacity;
};

struct SmooshResult {
  bool unimplemented;
  CVec<uint8_t> error;
  CVec<uint8_t> bytecode;
  CVec<CVec<uint8_t>> strings;
  /// Line and column numbers for the first character of source.
  uintptr_t lineno;
  uintptr_t column;
  /// Offset of main entry point from code, after predef'ing prologue.
  uintptr_t main_offset;
  /// Fixed frame slots.
  uint32_t max_fixed_slots;
  /// Maximum stack depth before any instruction.
  ///
  /// This value is a function of `bytecode`: there's only one correct value
  /// for a given script.
  uint32_t maximum_stack_depth;
  /// Index into the gcthings array of the body scope.
  uint32_t body_scope_index;
  /// Number of instructions in this script that have IC entries.
  ///
  /// A function of `bytecode`. See `JOF_IC`.
  uint32_t num_ic_entries;
  /// Number of instructions in this script that have JOF_TYPESET.
  uint32_t num_type_sets;
  /// `See BaseScript::ImmutableFlags`.
  bool strict;
  bool bindings_accessed_dynamically;
  bool has_call_site_obj;
  bool is_for_eval;
  bool is_module;
  bool is_function;
  bool has_non_syntactic_scope;
  bool needs_function_environment_objects;
  bool has_module_goal;
};

struct SmooshCompileOptions {
  bool no_script_rval;
};

extern "C" {

void free_smoosh(SmooshResult result);

SmooshResult run_smoosh(const uint8_t *text,
                        uintptr_t text_len,
                        const SmooshCompileOptions *options);

bool test_parse_module(const uint8_t *text, uintptr_t text_len);

bool test_parse_script(const uint8_t *text, uintptr_t text_len);

} // extern "C"
