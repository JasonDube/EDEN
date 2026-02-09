use std::collections::HashMap;

use crate::ast::*;
use crate::environment::Environment;
use crate::error::{GroveError, GroveResult};
use crate::types::Value;

/// Callback type for host-registered functions.
/// Takes args and returns a Value or error string.
pub type HostFn = Box<dyn Fn(&[Value]) -> Result<Value, String>>;

/// Control flow signals that propagate up through the call stack.
enum ControlFlow {
    Return(Value),
    Break,
    Continue,
}

pub struct Interpreter {
    pub env: Environment,
    host_fns: HashMap<String, HostFn>,
    blueprints: HashMap<String, (Vec<String>, Vec<Stmt>)>,
    instruction_count: u64,
    instruction_limit: u64,
    pub output: Vec<String>,
}

impl Interpreter {
    pub fn new() -> Self {
        Self {
            env: Environment::new(),
            host_fns: HashMap::new(),
            blueprints: HashMap::new(),
            instruction_count: 0,
            instruction_limit: 1_000_000,
            output: Vec::new(),
        }
    }

    pub fn set_instruction_limit(&mut self, limit: u64) {
        self.instruction_limit = limit;
    }

    pub fn register_fn(&mut self, name: &str, func: HostFn) {
        self.host_fns.insert(name.to_string(), func);
    }

    pub fn set_global(&mut self, name: &str, value: Value) {
        self.env.define(name, value);
    }

    pub fn execute(&mut self, program: &Program) -> GroveResult<Value> {
        self.instruction_count = 0;
        let mut last = Value::Nil;
        for stmt in &program.statements {
            match self.exec_stmt(stmt)? {
                Some(ControlFlow::Return(v)) => return Ok(v),
                Some(ControlFlow::Break) | Some(ControlFlow::Continue) => {
                    return Err(GroveError::runtime(
                        "break/continue outside of loop",
                        0, 0,
                    ));
                }
                None => {
                    last = Value::Nil;
                }
            }
        }
        let _ = last;
        Ok(Value::Nil)
    }

    fn tick(&mut self, line: usize, col: usize) -> GroveResult<()> {
        self.instruction_count += 1;
        if self.instruction_count > self.instruction_limit {
            Err(GroveError::instruction_limit(line, col))
        } else {
            Ok(())
        }
    }

    fn exec_stmt(&mut self, stmt: &Stmt) -> GroveResult<Option<ControlFlow>> {
        match stmt {
            Stmt::LocalDecl { name, init, span } => {
                self.tick(span.line, span.column)?;
                let val = match init {
                    Some(expr) => self.eval_expr(expr)?,
                    None => Value::Nil,
                };
                self.env.define(name, val);
                Ok(None)
            }

            Stmt::Assign { target, value, span } => {
                self.tick(span.line, span.column)?;
                let val = self.eval_expr(value)?;
                match target {
                    Expr::Ident { name, span: s } => {
                        if !self.env.set(name, val) {
                            return Err(GroveError::name_error(
                                format!("undefined variable '{}'", name),
                                s.line, s.column,
                            ));
                        }
                    }
                    Expr::FieldAccess { object, field, span: s } => {
                        let mut obj = self.eval_expr(object)?;
                        if let Value::Table(ref mut map) = obj {
                            map.insert(field.clone(), val);
                            // We need to write back — re-evaluate the base and set
                            // For now, table field assignment on local tables works
                            // through re-setting the base variable
                            self.set_value_at(object, obj)?;
                        } else {
                            return Err(GroveError::type_error(
                                format!("cannot set field '{}' on {}", field, obj.type_name()),
                                s.line, s.column,
                            ));
                        }
                    }
                    Expr::IndexAccess { object, index, span: s } => {
                        let idx = self.eval_expr(index)?;
                        let mut obj = self.eval_expr(object)?;
                        match (&mut obj, &idx) {
                            (Value::Array(arr), Value::Number(n)) => {
                                let i = *n as usize;
                                if i < arr.len() {
                                    arr[i] = val;
                                    self.set_value_at(object, obj)?;
                                } else {
                                    return Err(GroveError::runtime(
                                        format!("array index {} out of bounds (len {})", i, arr.len()),
                                        s.line, s.column,
                                    ));
                                }
                            }
                            (Value::Table(map), Value::String(key)) => {
                                map.insert(key.clone(), val);
                                self.set_value_at(object, obj)?;
                            }
                            _ => {
                                return Err(GroveError::type_error(
                                    format!("cannot index {} with {}", obj.type_name(), idx.type_name()),
                                    s.line, s.column,
                                ));
                            }
                        }
                    }
                    _ => {
                        return Err(GroveError::runtime(
                            "invalid assignment target",
                            span.line, span.column,
                        ));
                    }
                }
                Ok(None)
            }

            Stmt::ExprStmt { expr, span } => {
                self.tick(span.line, span.column)?;
                self.eval_expr(expr)?;
                Ok(None)
            }

            Stmt::If { condition, then_body, elseif_clauses, else_body, span } => {
                self.tick(span.line, span.column)?;
                let cond = self.eval_expr(condition)?;
                if cond.is_truthy() {
                    return self.exec_block(then_body);
                }
                for (elif_cond, elif_body) in elseif_clauses {
                    let cond = self.eval_expr(elif_cond)?;
                    if cond.is_truthy() {
                        return self.exec_block(elif_body);
                    }
                }
                if let Some(else_stmts) = else_body {
                    return self.exec_block(else_stmts);
                }
                Ok(None)
            }

            Stmt::While { condition, body, span } => {
                self.tick(span.line, span.column)?;
                loop {
                    let cond = self.eval_expr(condition)?;
                    if !cond.is_truthy() { break; }
                    match self.exec_block(body)? {
                        Some(ControlFlow::Break) => break,
                        Some(ControlFlow::Continue) => continue,
                        Some(cf @ ControlFlow::Return(_)) => return Ok(Some(cf)),
                        None => {}
                    }
                    self.tick(span.line, span.column)?;
                }
                Ok(None)
            }

            Stmt::NumericFor { var, start, limit, step, body, span } => {
                self.tick(span.line, span.column)?;
                let start_val = self.eval_expr(start)?.as_number().ok_or_else(|| {
                    GroveError::type_error("for start must be a number", span.line, span.column)
                })?;
                let limit_val = self.eval_expr(limit)?.as_number().ok_or_else(|| {
                    GroveError::type_error("for limit must be a number", span.line, span.column)
                })?;
                let step_val = match step {
                    Some(s) => self.eval_expr(s)?.as_number().ok_or_else(|| {
                        GroveError::type_error("for step must be a number", span.line, span.column)
                    })?,
                    None => 1.0,
                };

                if step_val == 0.0 {
                    return Err(GroveError::runtime("for step cannot be zero", span.line, span.column));
                }

                self.env.push_scope();
                let mut i = start_val;
                loop {
                    if step_val > 0.0 && i > limit_val { break; }
                    if step_val < 0.0 && i < limit_val { break; }

                    self.env.define(var, Value::Number(i));
                    self.tick(span.line, span.column)?;

                    match self.exec_block_no_scope(body)? {
                        Some(ControlFlow::Break) => break,
                        Some(ControlFlow::Continue) => {}
                        Some(cf @ ControlFlow::Return(_)) => {
                            self.env.pop_scope();
                            return Ok(Some(cf));
                        }
                        None => {}
                    }
                    i += step_val;
                }
                self.env.pop_scope();
                Ok(None)
            }

            Stmt::GenericFor { vars: _, iter: _, body: _, span } => {
                // Stub for M1 — generic for requires iterators
                Err(GroveError::runtime(
                    "generic for not yet implemented",
                    span.line, span.column,
                ))
            }

            Stmt::RepeatUntil { body, condition, span } => {
                self.tick(span.line, span.column)?;
                loop {
                    match self.exec_block(body)? {
                        Some(ControlFlow::Break) => break,
                        Some(ControlFlow::Continue) => {}
                        Some(cf @ ControlFlow::Return(_)) => return Ok(Some(cf)),
                        None => {}
                    }
                    let cond = self.eval_expr(condition)?;
                    if cond.is_truthy() { break; }
                    self.tick(span.line, span.column)?;
                }
                Ok(None)
            }

            Stmt::Blueprint { name, params, body, span } => {
                self.tick(span.line, span.column)?;
                self.blueprints.insert(name.clone(), (params.clone(), body.clone()));
                Ok(None)
            }

            Stmt::Build { name, args, span } => {
                self.tick(span.line, span.column)?;
                let (params, body) = self.blueprints.get(name).cloned().ok_or_else(|| {
                    GroveError::name_error(
                        format!("undefined blueprint '{}'", name),
                        span.line, span.column,
                    )
                })?;

                let mut arg_vals = Vec::new();
                for arg in args {
                    arg_vals.push(self.eval_expr(arg)?);
                }

                self.call_blueprint(&params, &arg_vals, &body, span)?;
                Ok(None)
            }

            Stmt::Return { value, span } => {
                self.tick(span.line, span.column)?;
                let val = match value {
                    Some(expr) => self.eval_expr(expr)?,
                    None => Value::Nil,
                };
                Ok(Some(ControlFlow::Return(val)))
            }

            Stmt::Break { span } => {
                self.tick(span.line, span.column)?;
                Ok(Some(ControlFlow::Break))
            }

            Stmt::Continue { span } => {
                self.tick(span.line, span.column)?;
                Ok(Some(ControlFlow::Continue))
            }
        }
    }

    fn exec_block(&mut self, stmts: &[Stmt]) -> GroveResult<Option<ControlFlow>> {
        self.env.push_scope();
        let result = self.exec_block_no_scope(stmts);
        self.env.pop_scope();
        result
    }

    fn exec_block_no_scope(&mut self, stmts: &[Stmt]) -> GroveResult<Option<ControlFlow>> {
        for stmt in stmts {
            if let Some(cf) = self.exec_stmt(stmt)? {
                return Ok(Some(cf));
            }
        }
        Ok(None)
    }

    fn call_blueprint(&mut self, params: &[String], args: &[Value], body: &[Stmt], _span: &Span) -> GroveResult<Value> {
        self.env.push_scope();
        for (i, param) in params.iter().enumerate() {
            let val = args.get(i).cloned().unwrap_or(Value::Nil);
            self.env.define(param, val);
        }

        let result = match self.exec_block_no_scope(body)? {
            Some(ControlFlow::Return(v)) => v,
            _ => Value::Nil,
        };

        self.env.pop_scope();
        Ok(result)
    }

    /// Helper to write back a value to the variable that an expression refers to.
    fn set_value_at(&mut self, expr: &Expr, value: Value) -> GroveResult<()> {
        if let Expr::Ident { name, span } = expr {
            if !self.env.set(name, value) {
                return Err(GroveError::name_error(
                    format!("undefined variable '{}'", name),
                    span.line, span.column,
                ));
            }
        }
        // For nested access (e.g., a.b.c = x), a full implementation would
        // recursively walk. For M1, single-level works.
        Ok(())
    }

    // ── Expression evaluation ───────────────────────────

    pub fn eval_expr(&mut self, expr: &Expr) -> GroveResult<Value> {
        match expr {
            Expr::NumberLit { value, .. } => Ok(Value::Number(*value)),
            Expr::StringLit { value, .. } => Ok(Value::String(value.clone())),
            Expr::BoolLit { value, .. } => Ok(Value::Bool(*value)),
            Expr::NilLit { .. } => Ok(Value::Nil),

            Expr::Ident { name, span } => {
                self.env.get(name).cloned().ok_or_else(|| {
                    GroveError::name_error(
                        format!("undefined variable '{}'", name),
                        span.line, span.column,
                    )
                })
            }

            Expr::BinaryOp { left, op, right, span } => {
                // Short-circuit for and/or
                match op {
                    BinOp::And => {
                        let l = self.eval_expr(left)?;
                        if !l.is_truthy() { return Ok(l); }
                        return self.eval_expr(right);
                    }
                    BinOp::Or => {
                        let l = self.eval_expr(left)?;
                        if l.is_truthy() { return Ok(l); }
                        return self.eval_expr(right);
                    }
                    _ => {}
                }

                let l = self.eval_expr(left)?;
                let r = self.eval_expr(right)?;
                self.eval_binary_op(op, &l, &r, span)
            }

            Expr::UnaryOp { op, operand, span } => {
                let val = self.eval_expr(operand)?;
                match op {
                    UnaryOp::Neg => {
                        if let Value::Number(n) = val {
                            Ok(Value::Number(-n))
                        } else {
                            Err(GroveError::type_error(
                                format!("cannot negate {}", val.type_name()),
                                span.line, span.column,
                            ))
                        }
                    }
                    UnaryOp::Not => Ok(Value::Bool(!val.is_truthy())),
                    UnaryOp::Len => {
                        match &val {
                            Value::String(s) => Ok(Value::Number(s.len() as f64)),
                            Value::Array(arr) => Ok(Value::Number(arr.len() as f64)),
                            Value::Table(map) => Ok(Value::Number(map.len() as f64)),
                            _ => Err(GroveError::type_error(
                                format!("cannot get length of {}", val.type_name()),
                                span.line, span.column,
                            )),
                        }
                    }
                }
            }

            Expr::Call { callee, args, span } => {
                // Evaluate arguments
                let mut arg_vals = Vec::new();
                for arg in args {
                    arg_vals.push(self.eval_expr(arg)?);
                }

                // Check for built-in vec3 constructor
                if let Expr::Ident { name, .. } = callee.as_ref() {
                    if name == "vec3" {
                        return self.builtin_vec3(&arg_vals, span);
                    }
                    // Check host functions
                    if let Some(func) = self.host_fns.get(name) {
                        // We need to call the host function. Since it's behind a shared ref
                        // and we have &mut self, we need to temporarily extract it.
                        // Use a raw pointer trick to avoid borrow issues.
                        let func_ptr = func as *const HostFn;
                        let result = unsafe { (*func_ptr)(&arg_vals) };
                        return result.map_err(|msg| {
                            GroveError::runtime(msg, span.line, span.column)
                        });
                    }
                    // Check blueprints (callable as functions)
                    if let Some((params, body)) = self.blueprints.get(name).cloned() {
                        return self.call_blueprint(&params, &arg_vals, &body, span);
                    }
                }

                Err(GroveError::name_error(
                    format!("undefined function '{}'", self.expr_name(callee)),
                    span.line, span.column,
                ))
            }

            Expr::FieldAccess { object, field, span } => {
                let obj = self.eval_expr(object)?;
                match &obj {
                    Value::Vec3(x, y, z) => {
                        match field.as_str() {
                            "x" => Ok(Value::Number(*x)),
                            "y" => Ok(Value::Number(*y)),
                            "z" => Ok(Value::Number(*z)),
                            _ => Err(GroveError::runtime(
                                format!("vec3 has no field '{}'", field),
                                span.line, span.column,
                            )),
                        }
                    }
                    Value::Table(map) => {
                        Ok(map.get(field).cloned().unwrap_or(Value::Nil))
                    }
                    _ => Err(GroveError::type_error(
                        format!("cannot access field '{}' on {}", field, obj.type_name()),
                        span.line, span.column,
                    )),
                }
            }

            Expr::IndexAccess { object, index, span } => {
                let obj = self.eval_expr(object)?;
                let idx = self.eval_expr(index)?;
                match (&obj, &idx) {
                    (Value::Array(arr), Value::Number(n)) => {
                        let i = *n as usize;
                        Ok(arr.get(i).cloned().unwrap_or(Value::Nil))
                    }
                    (Value::Table(map), Value::String(key)) => {
                        Ok(map.get(key).cloned().unwrap_or(Value::Nil))
                    }
                    (Value::String(s), Value::Number(n)) => {
                        let i = *n as usize;
                        Ok(s.chars().nth(i)
                            .map(|c| Value::String(c.to_string()))
                            .unwrap_or(Value::Nil))
                    }
                    _ => Err(GroveError::type_error(
                        format!("cannot index {} with {}", obj.type_name(), idx.type_name()),
                        span.line, span.column,
                    )),
                }
            }

            Expr::MethodCall { object, method, args, span } => {
                let obj = self.eval_expr(object)?;
                let mut arg_vals = Vec::new();
                for arg in args {
                    arg_vals.push(self.eval_expr(arg)?);
                }
                // For M1, method calls are not fully implemented
                Err(GroveError::runtime(
                    format!("method call '{}' on {} not yet implemented", method, obj.type_name()),
                    span.line, span.column,
                ))
            }

            Expr::ArrayLit { elements, .. } => {
                let mut arr = Vec::new();
                for elem in elements {
                    arr.push(self.eval_expr(elem)?);
                }
                Ok(Value::Array(arr))
            }

            Expr::TableLit { fields, .. } => {
                let mut map = HashMap::new();
                for (key, val_expr) in fields {
                    let val = self.eval_expr(val_expr)?;
                    map.insert(key.clone(), val);
                }
                Ok(Value::Table(map))
            }
        }
    }

    fn eval_binary_op(&self, op: &BinOp, left: &Value, right: &Value, span: &Span) -> GroveResult<Value> {
        match op {
            // Arithmetic
            BinOp::Add => self.numeric_op(left, right, |a, b| a + b, "+", span),
            BinOp::Sub => self.numeric_op(left, right, |a, b| a - b, "-", span),
            BinOp::Mul => self.numeric_op(left, right, |a, b| a * b, "*", span),
            BinOp::Div => {
                if let (Value::Number(_), Value::Number(b)) = (left, right) {
                    if *b == 0.0 {
                        return Err(GroveError::runtime("division by zero", span.line, span.column));
                    }
                }
                self.numeric_op(left, right, |a, b| a / b, "/", span)
            }
            BinOp::Mod => self.numeric_op(left, right, |a, b| a % b, "%", span),
            BinOp::Pow => self.numeric_op(left, right, |a, b| a.powf(b), "^", span),

            // String concatenation
            BinOp::Concat => {
                let l = format!("{}", left);
                let r = format!("{}", right);
                Ok(Value::String(format!("{}{}", l, r)))
            }

            // Comparison
            BinOp::Eq => Ok(Value::Bool(left == right)),
            BinOp::NotEq => Ok(Value::Bool(left != right)),
            BinOp::Lt => self.compare_op(left, right, |a, b| a < b, "<", span),
            BinOp::LtEq => self.compare_op(left, right, |a, b| a <= b, "<=", span),
            BinOp::Gt => self.compare_op(left, right, |a, b| a > b, ">", span),
            BinOp::GtEq => self.compare_op(left, right, |a, b| a >= b, ">=", span),

            // And/Or handled in eval_expr for short-circuit
            BinOp::And | BinOp::Or => unreachable!(),
        }
    }

    fn numeric_op(&self, left: &Value, right: &Value, f: impl Fn(f64, f64) -> f64, op_name: &str, span: &Span) -> GroveResult<Value> {
        match (left, right) {
            (Value::Number(a), Value::Number(b)) => Ok(Value::Number(f(*a, *b))),
            // Vec3 arithmetic
            (Value::Vec3(ax, ay, az), Value::Vec3(bx, by, bz)) if op_name == "+" || op_name == "-" => {
                Ok(Value::Vec3(f(*ax, *bx), f(*ay, *by), f(*az, *bz)))
            }
            (Value::Vec3(ax, ay, az), Value::Number(b)) if op_name == "*" || op_name == "/" => {
                Ok(Value::Vec3(f(*ax, *b), f(*ay, *b), f(*az, *b)))
            }
            (Value::Number(a), Value::Vec3(bx, by, bz)) if op_name == "*" => {
                Ok(Value::Vec3(f(*a, *bx), f(*a, *by), f(*a, *bz)))
            }
            _ => Err(GroveError::type_error(
                format!("cannot apply '{}' to {} and {}", op_name, left.type_name(), right.type_name()),
                span.line, span.column,
            )),
        }
    }

    fn compare_op(&self, left: &Value, right: &Value, f: impl Fn(f64, f64) -> bool, op_name: &str, span: &Span) -> GroveResult<Value> {
        match (left, right) {
            (Value::Number(a), Value::Number(b)) => Ok(Value::Bool(f(*a, *b))),
            (Value::String(a), Value::String(b)) => {
                let cmp = a.cmp(b);
                let result = match op_name {
                    "<" => cmp == std::cmp::Ordering::Less,
                    "<=" => cmp != std::cmp::Ordering::Greater,
                    ">" => cmp == std::cmp::Ordering::Greater,
                    ">=" => cmp != std::cmp::Ordering::Less,
                    _ => false,
                };
                Ok(Value::Bool(result))
            }
            _ => Err(GroveError::type_error(
                format!("cannot compare {} and {} with '{}'", left.type_name(), right.type_name(), op_name),
                span.line, span.column,
            )),
        }
    }

    fn builtin_vec3(&self, args: &[Value], span: &Span) -> GroveResult<Value> {
        if args.len() != 3 {
            return Err(GroveError::runtime(
                format!("vec3() expects 3 arguments, got {}", args.len()),
                span.line, span.column,
            ));
        }
        let x = args[0].as_number().ok_or_else(|| {
            GroveError::type_error("vec3 x must be a number", span.line, span.column)
        })?;
        let y = args[1].as_number().ok_or_else(|| {
            GroveError::type_error("vec3 y must be a number", span.line, span.column)
        })?;
        let z = args[2].as_number().ok_or_else(|| {
            GroveError::type_error("vec3 z must be a number", span.line, span.column)
        })?;
        Ok(Value::Vec3(x, y, z))
    }

    fn expr_name(&self, expr: &Expr) -> String {
        match expr {
            Expr::Ident { name, .. } => name.clone(),
            _ => "<expression>".to_string(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    fn run(src: &str) -> (GroveResult<Value>, Vec<String>) {
        let mut lex = Lexer::new(src);
        let tokens = lex.tokenize().unwrap();
        let mut parser = Parser::new(tokens);
        let program = parser.parse().unwrap();
        let mut interp = Interpreter::new();

        // Register a log function that captures output
        let output = std::rc::Rc::new(std::cell::RefCell::new(Vec::new()));
        let out_clone = output.clone();
        interp.register_fn("log", Box::new(move |args: &[Value]| {
            let msg: Vec<String> = args.iter().map(|v| format!("{}", v)).collect();
            out_clone.borrow_mut().push(msg.join(" "));
            Ok(Value::Nil)
        }));

        let result = interp.execute(&program);
        let captured = output.borrow().clone();
        (result, captured)
    }

    #[test]
    fn test_basic_arithmetic() {
        let (result, output) = run("local x = 10\nlocal y = x * 2 + 5\nlog(y)");
        assert!(result.is_ok());
        assert_eq!(output, vec!["25"]);
    }

    #[test]
    fn test_string_concat() {
        let (_, output) = run(r#"local a = "hello" .. " " .. "world"
log(a)"#);
        assert_eq!(output, vec!["hello world"]);
    }

    #[test]
    fn test_if_else() {
        let (_, output) = run(r#"
local x = 15
if x > 10 then
    log("big")
elseif x > 5 then
    log("medium")
else
    log("small")
end
"#);
        assert_eq!(output, vec!["big"]);
    }

    #[test]
    fn test_while_loop() {
        let (_, output) = run(r#"
local i = 0
local sum = 0
while i < 5 do
    sum = sum + i
    i = i + 1
end
log(sum)
"#);
        assert_eq!(output, vec!["10"]);
    }

    #[test]
    fn test_numeric_for() {
        let (_, output) = run(r#"
local sum = 0
for i = 1, 5 do
    sum = sum + i
end
log(sum)
"#);
        assert_eq!(output, vec!["15"]);
    }

    #[test]
    fn test_numeric_for_with_step() {
        let (_, output) = run(r#"
local sum = 0
for i = 10, 1, -2 do
    sum = sum + i
end
log(sum)
"#);
        // 10 + 8 + 6 + 4 + 2 = 30
        assert_eq!(output, vec!["30"]);
    }

    #[test]
    fn test_blueprint_and_build() {
        let (_, output) = run(r#"
blueprint greet(name)
    log("hello " .. name)
end
build greet("world")
"#);
        assert_eq!(output, vec!["hello world"]);
    }

    #[test]
    fn test_blueprint_as_function() {
        let (_, output) = run(r#"
blueprint add(a, b)
    return a + b
end
local result = add(3, 4)
log(result)
"#);
        assert_eq!(output, vec!["7"]);
    }

    #[test]
    fn test_vec3() {
        let (_, output) = run(r#"
local pos = vec3(1.0, 2.0, 3.0)
log(pos.x)
log(pos.y)
log(pos.z)
"#);
        assert_eq!(output, vec!["1", "2", "3"]);
    }

    #[test]
    fn test_array() {
        let (_, output) = run(r#"
local arr = [10, 20, 30]
log(arr[0])
log(arr[1])
log(#arr)
"#);
        assert_eq!(output, vec!["10", "20", "3"]);
    }

    #[test]
    fn test_table() {
        let (_, output) = run(r#"
local t = {name = "foo", size = 4}
log(t.name)
log(t.size)
"#);
        assert_eq!(output, vec!["foo", "4"]);
    }

    #[test]
    fn test_boolean_ops() {
        let (_, output) = run(r#"
log(true and false)
log(true or false)
log(not true)
"#);
        assert_eq!(output, vec!["false", "true", "false"]);
    }

    #[test]
    fn test_comparison() {
        let (_, output) = run(r#"
log(5 > 3)
log(5 < 3)
log(5 == 5)
log(5 ~= 3)
"#);
        assert_eq!(output, vec!["true", "false", "true", "true"]);
    }

    #[test]
    fn test_instruction_limit() {
        let mut lex = Lexer::new("while true do\nend");
        let tokens = lex.tokenize().unwrap();
        let mut parser = Parser::new(tokens);
        let program = parser.parse().unwrap();
        let mut interp = Interpreter::new();
        interp.set_instruction_limit(100);
        let result = interp.execute(&program);
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert_eq!(err.kind, crate::error::ErrorKind::InstructionLimit);
    }

    #[test]
    fn test_undefined_variable() {
        let mut lex = Lexer::new("log(x)");
        let tokens = lex.tokenize().unwrap();
        let mut parser = Parser::new(tokens);
        let program = parser.parse().unwrap();
        let mut interp = Interpreter::new();
        interp.register_fn("log", Box::new(|_: &[Value]| Ok(Value::Nil)));
        let result = interp.execute(&program);
        assert!(result.is_err());
    }

    #[test]
    fn test_break_in_while() {
        let (_, output) = run(r#"
local i = 0
while true do
    if i >= 3 then
        break
    end
    log(i)
    i = i + 1
end
"#);
        assert_eq!(output, vec!["0", "1", "2"]);
    }

    #[test]
    fn test_continue_in_for() {
        let (_, output) = run(r#"
for i = 1, 5 do
    if i == 3 then
        continue
    end
    log(i)
end
"#);
        assert_eq!(output, vec!["1", "2", "4", "5"]);
    }

    #[test]
    fn test_repeat_until() {
        let (_, output) = run(r#"
local i = 0
repeat
    log(i)
    i = i + 1
until i >= 3
"#);
        assert_eq!(output, vec!["0", "1", "2"]);
    }

    #[test]
    fn test_nested_scopes() {
        let (_, output) = run(r#"
local x = 1
if true then
    local x = 2
    log(x)
end
log(x)
"#);
        assert_eq!(output, vec!["2", "1"]);
    }

    #[test]
    fn test_power_right_assoc() {
        let (_, output) = run(r#"
-- 2^3^2 should be 2^(3^2) = 2^9 = 512
log(2 ^ 3 ^ 2)
"#);
        assert_eq!(output, vec!["512"]);
    }

    #[test]
    fn test_unary_minus() {
        let (_, output) = run(r#"log(-5 + 3)"#);
        assert_eq!(output, vec!["-2"]);
    }

    #[test]
    fn test_nil_equality() {
        let (_, output) = run(r#"
log(nil == nil)
log(nil ~= 5)
"#);
        assert_eq!(output, vec!["true", "true"]);
    }

    #[test]
    fn test_string_escape() {
        let (_, output) = run(r#"log("hello\tworld\n")"#);
        assert_eq!(output, vec!["hello\tworld\n"]);
    }
}
