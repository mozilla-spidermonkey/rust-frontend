#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <new>

struct Bytecode {
  uint8_t *data;
  uintptr_t len;
  uintptr_t capacity;
};

extern "C" {

void asdf();

void free_bytecode(Bytecode bytecode);

Bytecode run_jsparagus(const uint8_t *text, uintptr_t text_len);

} // extern "C"
