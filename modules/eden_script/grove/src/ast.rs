/// AST node types for the Grove language.

#[derive(Debug, Clone)]
pub struct Program {
    pub statements: Vec<Stmt>,
}

#[derive(Debug, Clone)]
pub struct Span {
    pub line: usize,
    pub column: usize,
}

#[derive(Debug, Clone)]
pub enum Stmt {
    /// `local x = expr` or `local x`
    LocalDecl {
        name: String,
        init: Option<Expr>,
        span: Span,
    },
    /// `x = expr` (assignment to existing variable)
    Assign {
        target: Expr,
        value: Expr,
        span: Span,
    },
    /// Expression used as a statement (function calls, etc.)
    ExprStmt {
        expr: Expr,
        span: Span,
    },
    /// `if cond then ... elseif cond then ... else ... end`
    If {
        condition: Expr,
        then_body: Vec<Stmt>,
        elseif_clauses: Vec<(Expr, Vec<Stmt>)>,
        else_body: Option<Vec<Stmt>>,
        span: Span,
    },
    /// `while cond do ... end`
    While {
        condition: Expr,
        body: Vec<Stmt>,
        span: Span,
    },
    /// `for var = start, limit [, step] do ... end`
    NumericFor {
        var: String,
        start: Expr,
        limit: Expr,
        step: Option<Expr>,
        body: Vec<Stmt>,
        span: Span,
    },
    /// `for k, v in expr do ... end`
    GenericFor {
        vars: Vec<String>,
        iter: Expr,
        body: Vec<Stmt>,
        span: Span,
    },
    /// `repeat ... until cond`
    RepeatUntil {
        body: Vec<Stmt>,
        condition: Expr,
        span: Span,
    },
    /// `blueprint name(params) ... end`
    Blueprint {
        name: String,
        params: Vec<String>,
        body: Vec<Stmt>,
        span: Span,
    },
    /// `build name(args)`
    Build {
        name: String,
        args: Vec<Expr>,
        span: Span,
    },
    /// `return [expr]`
    Return {
        value: Option<Expr>,
        span: Span,
    },
    /// `break`
    Break { span: Span },
    /// `continue`
    Continue { span: Span },
}

#[derive(Debug, Clone)]
pub enum Expr {
    /// Number literal
    NumberLit { value: f64, span: Span },
    /// String literal
    StringLit { value: String, span: Span },
    /// `true` or `false`
    BoolLit { value: bool, span: Span },
    /// `nil`
    NilLit { span: Span },
    /// Variable reference
    Ident { name: String, span: Span },
    /// Binary operation: `a + b`, `a and b`, etc.
    BinaryOp {
        left: Box<Expr>,
        op: BinOp,
        right: Box<Expr>,
        span: Span,
    },
    /// Unary operation: `-x`, `not x`, `#arr`
    UnaryOp {
        op: UnaryOp,
        operand: Box<Expr>,
        span: Span,
    },
    /// Function call: `f(a, b, c)`
    Call {
        callee: Box<Expr>,
        args: Vec<Expr>,
        span: Span,
    },
    /// Field access: `obj.field`
    FieldAccess {
        object: Box<Expr>,
        field: String,
        span: Span,
    },
    /// Index access: `arr[idx]`
    IndexAccess {
        object: Box<Expr>,
        index: Box<Expr>,
        span: Span,
    },
    /// Method call: `obj:method(args)`
    MethodCall {
        object: Box<Expr>,
        method: String,
        args: Vec<Expr>,
        span: Span,
    },
    /// Array literal: `[1, 2, 3]`
    ArrayLit {
        elements: Vec<Expr>,
        span: Span,
    },
    /// Table literal: `{a = 1, b = 2}`
    TableLit {
        fields: Vec<(String, Expr)>,
        span: Span,
    },
}

impl Expr {
    pub fn span(&self) -> &Span {
        match self {
            Expr::NumberLit { span, .. }
            | Expr::StringLit { span, .. }
            | Expr::BoolLit { span, .. }
            | Expr::NilLit { span }
            | Expr::Ident { span, .. }
            | Expr::BinaryOp { span, .. }
            | Expr::UnaryOp { span, .. }
            | Expr::Call { span, .. }
            | Expr::FieldAccess { span, .. }
            | Expr::IndexAccess { span, .. }
            | Expr::MethodCall { span, .. }
            | Expr::ArrayLit { span, .. }
            | Expr::TableLit { span, .. } => span,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum BinOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Pow,
    Concat,
    Eq,
    NotEq,
    Lt,
    LtEq,
    Gt,
    GtEq,
    And,
    Or,
}

#[derive(Debug, Clone, PartialEq)]
pub enum UnaryOp {
    Neg,
    Not,
    Len,
}
