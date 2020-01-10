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
  CVec<uint8_t> bytecode;
  CVec<CVec<uint8_t>> strings;
  CVec<uint8_t> error;
  bool unimplemented;
};

extern "C" {

void free_jsparagus(JsparagusResult result);

JsparagusResult run_jsparagus(const uint8_t *text, uintptr_t text_len);

bool test_parse_module(const uint8_t *text, uintptr_t text_len);

bool test_parse_script(const uint8_t *text, uintptr_t text_len);

} // extern "C"
