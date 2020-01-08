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

struct JsparagusResult {
  bool unimplemented;
  CVec<uint8_t> error;
  CVec<uint8_t> bytecode;
  CVec<CVec<uint8_t>> strings;
  /// Maximum stack depth before any instruction.
  ///
  /// This value is a function of `bytecode`: there's only one correct value
  /// for a given script.
  uint32_t maximum_stack_depth;
  /// Number of instructions in this script that have IC entries.
  ///
  /// A function of `bytecode`. See `JOF_IC`.
  uint32_t num_ic_entries;
};

extern "C" {

void free_jsparagus(JsparagusResult result);

JsparagusResult run_jsparagus(const uint8_t *text, uintptr_t text_len);

bool test_parse_module(const uint8_t *text, uintptr_t text_len);

bool test_parse_script(const uint8_t *text, uintptr_t text_len);

} // extern "C"
