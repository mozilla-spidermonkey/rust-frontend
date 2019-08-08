extern crate parser;

use ast::Program;
use emitter::emit;
use parser::parse_script;
use std::mem;
use std::slice;
use std::str;

#[repr(C)]
pub struct Bytecode {
    data: *mut u8,
    len: usize,
    capacity: usize,
}

#[no_mangle]
pub unsafe extern "C" fn run_jsparagus(text: *const u8, text_len: usize) -> Bytecode {
    let text = str::from_utf8(slice::from_raw_parts(text, text_len)).expect("Invalid UTF8");
    let mut bytecode = jsparagus(text);
    let result = Bytecode {
        data: bytecode.as_mut_ptr(),
        len: bytecode.len(),
        capacity: bytecode.capacity(),
    };
    mem::forget(bytecode);
    result
}

#[no_mangle]
pub unsafe extern "C" fn free_bytecode(bytecode: Bytecode) {
    Vec::from_raw_parts(bytecode.data, bytecode.len, bytecode.capacity);
}

fn jsparagus(text: &str) -> Vec<u8> {
    let parse_result = parse_script(text).expect("Failed to parse");
    emit(&Program::Script(*parse_result))
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        assert_eq!(2 + 2, 4);
    }
}
