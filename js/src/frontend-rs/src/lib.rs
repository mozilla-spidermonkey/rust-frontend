extern crate parser;

use ast::types::Program;
use bumpalo;
use emitter::{emit, EmitResult, EmitError};
use parser::{parse_module, parse_script};
use std::{mem, slice, str};

#[repr(C)]
pub struct CVec<T> {
    pub data: *mut T,
    pub len: usize,
    pub capacity: usize,
}

impl<T> CVec<T> {
    fn empty() -> CVec<T> {
        Self {
            data: std::ptr::null_mut(),
            len: 0,
            capacity: 0,
        }
    }

    fn from(mut v: Vec<T>) -> CVec<T> {
        let result = Self {
            data: v.as_mut_ptr(),
            len: v.len(),
            capacity: v.capacity(),
        };
        mem::forget(v);
        result
    }

    unsafe fn into(self) -> Vec<T> {
        Vec::from_raw_parts(self.data, self.len, self.capacity)
    }
}

#[repr(C)]
pub struct JsparagusResult {
    bytecode: CVec<u8>,
    strings: CVec<CVec<u8>>,
    unimplemented: bool,
}

#[no_mangle]
pub unsafe extern "C" fn run_jsparagus(text: *const u8, text_len: usize) -> JsparagusResult {
    let text = str::from_utf8(slice::from_raw_parts(text, text_len)).expect("Invalid UTF8");
    match jsparagus(text) {
        Ok(mut result) =>
            JsparagusResult {
                bytecode: CVec::from(result.bytecode),
                strings: CVec::from(
                    result
                        .strings
                        .drain(..)
                        .map(|s| CVec::from(s.into_bytes()))
                        .collect(),
                ),
                unimplemented: false,
            },
        Err(EmitError::NotImplemented(message)) => {
            println!("Unimplemented: {}", message);
            JsparagusResult {
                bytecode: CVec::empty(),
                strings: CVec::empty(),
                unimplemented: true,
            }
        },
    }
}

#[no_mangle]
pub unsafe extern "C" fn test_parse_script(text: *const u8, text_len: usize) -> bool {
    let text = match str::from_utf8(slice::from_raw_parts(text, text_len)) {
        Ok(text) => text,
        Err(_) => return false, // .expect("Invalid UTF8")
    };
    let allocator = bumpalo::Bump::new();
    match parse_script(&allocator, text) {
        Ok(_) => true,
        Err(_) => false,
    }
}


#[no_mangle]
pub unsafe extern "C" fn test_parse_module(text: *const u8, text_len: usize) -> bool {
    let text = match str::from_utf8(slice::from_raw_parts(text, text_len)) {
        Ok(text) => text,
        Err(_) => return false, // .expect("Invalid UTF8")
    };
    let allocator = bumpalo::Bump::new();
    match parse_module(&allocator, text) {
        Ok(_) => true,
        Err(_) => false,
    }
}

#[no_mangle]
pub unsafe extern "C" fn free_jsparagus(result: JsparagusResult) {
    let _ = result.bytecode.into();
    for v in result.strings.into() {
        let _ = v.into();
    }
    //Vec::from_raw_parts(bytecode.data, bytecode.len, bytecode.capacity);
}

fn jsparagus(text: &str) -> Result<EmitResult, EmitError> {
    let allocator = bumpalo::Bump::new();
    let parse_result = parse_script(&allocator, text).expect("Failed to parse");
    emit(&mut Program::Script(parse_result.unbox()))
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        assert_eq!(2 + 2, 4);
    }
}
