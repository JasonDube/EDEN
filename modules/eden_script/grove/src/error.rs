use std::fmt;

#[derive(Debug, Clone, PartialEq)]
pub enum ErrorKind {
    Syntax,
    Runtime,
    Type,
    NameError,
    InstructionLimit,
}

#[derive(Debug, Clone)]
pub struct GroveError {
    pub kind: ErrorKind,
    pub message: String,
    pub line: usize,
    pub column: usize,
}

impl GroveError {
    pub fn syntax(message: impl Into<String>, line: usize, column: usize) -> Self {
        Self { kind: ErrorKind::Syntax, message: message.into(), line, column }
    }

    pub fn runtime(message: impl Into<String>, line: usize, column: usize) -> Self {
        Self { kind: ErrorKind::Runtime, message: message.into(), line, column }
    }

    pub fn type_error(message: impl Into<String>, line: usize, column: usize) -> Self {
        Self { kind: ErrorKind::Type, message: message.into(), line, column }
    }

    pub fn name_error(message: impl Into<String>, line: usize, column: usize) -> Self {
        Self { kind: ErrorKind::NameError, message: message.into(), line, column }
    }

    pub fn instruction_limit(line: usize, column: usize) -> Self {
        Self {
            kind: ErrorKind::InstructionLimit,
            message: "instruction limit exceeded".into(),
            line,
            column,
        }
    }
}

impl fmt::Display for GroveError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "[line {}:{}] {:?}: {}", self.line, self.column, self.kind, self.message)
    }
}

impl std::error::Error for GroveError {}

pub type GroveResult<T> = Result<T, GroveError>;
