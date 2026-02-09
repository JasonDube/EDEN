pub mod error;
pub mod types;
pub mod lexer;
pub mod ast;
pub mod parser;
pub mod environment;
pub mod interpreter;

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_void};
use std::ptr;

use crate::interpreter::{HostFn, Interpreter};
use crate::lexer::Lexer;
use crate::parser::Parser;
use crate::types::Value;

// ── FFI Value types ─────────────────────────────────

#[repr(C)]
#[derive(Clone, Copy)]
pub enum GroveValueTag {
    Nil = 0,
    Bool = 1,
    Number = 2,
    String = 3,
    Vec3 = 4,
    Object = 5,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GroveStringVal {
    pub ptr: *const c_char,
    pub len: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GroveVec3Val {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

#[repr(C)]
pub union GroveValueData {
    pub bool_val: i32,
    pub number_val: f64,
    pub string_val: GroveStringVal,
    pub vec3_val: GroveVec3Val,
    pub object_handle: u64,
}

#[repr(C)]
pub struct GroveValue {
    pub tag: GroveValueTag,
    pub data: GroveValueData,
}

/// Host function callback type from C.
pub type GroveHostFn = extern "C" fn(
    args: *const GroveValue,
    arg_count: u32,
    result: *mut GroveValue,
    userdata: *mut c_void,
) -> i32;

// ── VM struct ───────────────────────────────────────

pub struct GroveVm {
    interp: Interpreter,
    last_error: Option<CString>,
    last_error_line: u32,
    /// Temporary storage for strings returned via FFI
    _temp_strings: Vec<CString>,
}

// ── Conversion helpers ──────────────────────────────

fn grove_value_to_value(gv: &GroveValue) -> Value {
    unsafe {
        match gv.tag {
            GroveValueTag::Nil => Value::Nil,
            GroveValueTag::Bool => Value::Bool(gv.data.bool_val != 0),
            GroveValueTag::Number => Value::Number(gv.data.number_val),
            GroveValueTag::String => {
                let sv = &gv.data.string_val;
                if sv.ptr.is_null() {
                    Value::String(String::new())
                } else {
                    let slice = std::slice::from_raw_parts(sv.ptr as *const u8, sv.len as usize);
                    Value::String(String::from_utf8_lossy(slice).into_owned())
                }
            }
            GroveValueTag::Vec3 => {
                let v = &gv.data.vec3_val;
                Value::Vec3(v.x, v.y, v.z)
            }
            GroveValueTag::Object => Value::Object(gv.data.object_handle),
        }
    }
}

fn value_to_grove_value(val: &Value) -> GroveValue {
    match val {
        Value::Nil => GroveValue {
            tag: GroveValueTag::Nil,
            data: GroveValueData { bool_val: 0 },
        },
        Value::Bool(b) => GroveValue {
            tag: GroveValueTag::Bool,
            data: GroveValueData { bool_val: if *b { 1 } else { 0 } },
        },
        Value::Number(n) => GroveValue {
            tag: GroveValueTag::Number,
            data: GroveValueData { number_val: *n },
        },
        Value::String(s) => {
            // Note: the string pointer here is only valid as long as `val` lives.
            // For FFI callbacks this is fine — the C side copies what it needs.
            GroveValue {
                tag: GroveValueTag::String,
                data: GroveValueData {
                    string_val: GroveStringVal {
                        ptr: s.as_ptr() as *const c_char,
                        len: s.len() as u32,
                    },
                },
            }
        }
        Value::Vec3(x, y, z) => GroveValue {
            tag: GroveValueTag::Vec3,
            data: GroveValueData {
                vec3_val: GroveVec3Val { x: *x, y: *y, z: *z },
            },
        },
        Value::Object(handle) => GroveValue {
            tag: GroveValueTag::Object,
            data: GroveValueData { object_handle: *handle },
        },
        // Array and Table don't have FFI representation — return Nil
        _ => GroveValue {
            tag: GroveValueTag::Nil,
            data: GroveValueData { bool_val: 0 },
        },
    }
}

// ── C FFI exports ───────────────────────────────────

#[no_mangle]
pub extern "C" fn grove_new() -> *mut GroveVm {
    let vm = Box::new(GroveVm {
        interp: Interpreter::new(),
        last_error: None,
        last_error_line: 0,
        _temp_strings: Vec::new(),
    });
    Box::into_raw(vm)
}

#[no_mangle]
pub unsafe extern "C" fn grove_destroy(vm: *mut GroveVm) {
    if !vm.is_null() {
        drop(Box::from_raw(vm));
    }
}

#[no_mangle]
pub unsafe extern "C" fn grove_eval(vm: *mut GroveVm, source: *const c_char) -> i32 {
    if vm.is_null() || source.is_null() {
        return -1;
    }
    let vm = &mut *vm;
    let src = match CStr::from_ptr(source).to_str() {
        Ok(s) => s,
        Err(_) => {
            vm.last_error = Some(CString::new("invalid UTF-8 in source").unwrap());
            vm.last_error_line = 0;
            return -1;
        }
    };

    // Lex
    let mut lexer = Lexer::new(src);
    let tokens = match lexer.tokenize() {
        Ok(t) => t,
        Err(e) => {
            vm.last_error_line = e.line as u32;
            vm.last_error = CString::new(format!("{}", e)).ok();
            return -1;
        }
    };

    // Parse
    let mut parser = Parser::new(tokens);
    let program = match parser.parse() {
        Ok(p) => p,
        Err(e) => {
            vm.last_error_line = e.line as u32;
            vm.last_error = CString::new(format!("{}", e)).ok();
            return -1;
        }
    };

    // Execute
    match vm.interp.execute(&program) {
        Ok(_) => {
            vm.last_error = None;
            vm.last_error_line = 0;
            0
        }
        Err(e) => {
            vm.last_error_line = e.line as u32;
            vm.last_error = CString::new(format!("{}", e)).ok();
            -1
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn grove_register_fn(
    vm: *mut GroveVm,
    name: *const c_char,
    callback: GroveHostFn,
    userdata: *mut c_void,
) -> i32 {
    if vm.is_null() || name.is_null() {
        return -1;
    }
    let vm = &mut *vm;
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return -1,
    };

    // Wrap the C callback in a Rust closure.
    // userdata is a raw pointer — the C side is responsible for its lifetime.
    let ud = userdata as usize; // make it Send-safe
    let err_name = name_str.clone();
    let host_fn: HostFn = Box::new(move |args: &[Value]| {
        let ffi_args: Vec<GroveValue> = args.iter().map(|a| value_to_grove_value(a)).collect();
        let mut result = GroveValue {
            tag: GroveValueTag::Nil,
            data: GroveValueData { bool_val: 0 },
        };
        let ret = callback(
            ffi_args.as_ptr(),
            ffi_args.len() as u32,
            &mut result,
            ud as *mut c_void,
        );
        if ret == 0 {
            Ok(grove_value_to_value(&result))
        } else {
            Err(format!("host function '{}' returned error code {}", err_name, ret))
        }
    });

    vm.interp.register_fn(&name_str, host_fn);
    0
}

#[no_mangle]
pub unsafe extern "C" fn grove_set_global_number(
    vm: *mut GroveVm,
    name: *const c_char,
    value: f64,
) -> i32 {
    if vm.is_null() || name.is_null() { return -1; }
    let vm = &mut *vm;
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };
    vm.interp.set_global(name_str, Value::Number(value));
    0
}

#[no_mangle]
pub unsafe extern "C" fn grove_set_global_string(
    vm: *mut GroveVm,
    name: *const c_char,
    value: *const c_char,
) -> i32 {
    if vm.is_null() || name.is_null() || value.is_null() { return -1; }
    let vm = &mut *vm;
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };
    let val_str = match CStr::from_ptr(value).to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };
    vm.interp.set_global(name_str, Value::String(val_str.to_string()));
    0
}

#[no_mangle]
pub unsafe extern "C" fn grove_set_global_vec3(
    vm: *mut GroveVm,
    name: *const c_char,
    x: f64, y: f64, z: f64,
) -> i32 {
    if vm.is_null() || name.is_null() { return -1; }
    let vm = &mut *vm;
    let name_str = match CStr::from_ptr(name).to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };
    vm.interp.set_global(name_str, Value::Vec3(x, y, z));
    0
}

#[no_mangle]
pub unsafe extern "C" fn grove_last_error(vm: *const GroveVm) -> *const c_char {
    if vm.is_null() { return ptr::null(); }
    let vm = &*vm;
    match &vm.last_error {
        Some(e) => e.as_ptr(),
        None => ptr::null(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn grove_last_error_line(vm: *const GroveVm) -> u32 {
    if vm.is_null() { return 0; }
    let vm = &*vm;
    vm.last_error_line
}

#[no_mangle]
pub unsafe extern "C" fn grove_set_instruction_limit(vm: *mut GroveVm, limit: u64) {
    if vm.is_null() { return; }
    let vm = &mut *vm;
    vm.interp.set_instruction_limit(limit);
}

// ── Integration test from Rust side ─────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ffi_eval() {
        unsafe {
            let vm = grove_new();
            assert!(!vm.is_null());

            // Register a log function
            extern "C" fn log_fn(
                args: *const GroveValue,
                arg_count: u32,
                _result: *mut GroveValue,
                _userdata: *mut c_void,
            ) -> i32 {
                unsafe {
                    for i in 0..arg_count {
                        let arg = &*args.add(i as usize);
                        match arg.tag {
                            GroveValueTag::Number => {
                                let n = arg.data.number_val;
                                if n == (n as i64) as f64 && n.is_finite() {
                                    print!("{}", n as i64);
                                } else {
                                    print!("{}", n);
                                }
                            }
                            GroveValueTag::String => {
                                let sv = &arg.data.string_val;
                                let slice = std::slice::from_raw_parts(
                                    sv.ptr as *const u8,
                                    sv.len as usize,
                                );
                                print!("{}", String::from_utf8_lossy(slice));
                            }
                            _ => print!("?"),
                        }
                        if i + 1 < arg_count { print!(" "); }
                    }
                    println!();
                }
                0
            }

            let name = CString::new("log").unwrap();
            grove_register_fn(vm, name.as_ptr(), log_fn, ptr::null_mut());

            let source = CString::new("local x = 10\nlocal y = x * 2 + 5\nlog(y)").unwrap();
            let ret = grove_eval(vm, source.as_ptr());
            assert_eq!(ret, 0, "eval should succeed");

            // Test error reporting
            let bad_source = CString::new("local x = 10 +").unwrap();
            let ret = grove_eval(vm, bad_source.as_ptr());
            assert_eq!(ret, -1, "eval should fail on syntax error");
            let err = grove_last_error(vm);
            assert!(!err.is_null());
            let err_line = grove_last_error_line(vm);
            assert!(err_line > 0);

            // Test instruction limit
            grove_set_instruction_limit(vm, 50);
            let loop_source = CString::new("while true do\nend").unwrap();
            let ret = grove_eval(vm, loop_source.as_ptr());
            assert_eq!(ret, -1, "eval should fail on infinite loop");

            grove_destroy(vm);
        }
    }

    #[test]
    fn test_ffi_globals() {
        unsafe {
            let vm = grove_new();

            extern "C" fn log_fn(
                _args: *const GroveValue,
                _arg_count: u32,
                _result: *mut GroveValue,
                _userdata: *mut c_void,
            ) -> i32 {
                0
            }

            let name = CString::new("log").unwrap();
            grove_register_fn(vm, name.as_ptr(), log_fn, ptr::null_mut());

            let gname = CString::new("my_num").unwrap();
            grove_set_global_number(vm, gname.as_ptr(), 42.0);

            let sname = CString::new("my_str").unwrap();
            let sval = CString::new("hello").unwrap();
            grove_set_global_string(vm, sname.as_ptr(), sval.as_ptr());

            let vname = CString::new("my_pos").unwrap();
            grove_set_global_vec3(vm, vname.as_ptr(), 1.0, 2.0, 3.0);

            let source = CString::new("log(my_num)\nlog(my_str)\nlog(my_pos.x)").unwrap();
            let ret = grove_eval(vm, source.as_ptr());
            assert_eq!(ret, 0);

            grove_destroy(vm);
        }
    }
}
