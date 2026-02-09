use crate::error::{GroveError, GroveResult};

#[derive(Debug, Clone, PartialEq)]
pub enum TokenKind {
    // Literals
    Number(f64),
    StringLit(String),
    True,
    False,
    Nil,

    // Identifier
    Identifier(String),

    // Keywords
    Local,
    Let,
    Fn,
    Blueprint,
    Build,
    End,
    If,
    Then,
    ElseIf,
    Else,
    For,
    In,
    Do,
    While,
    Repeat,
    Until,
    Return,
    Break,
    Continue,
    And,
    Or,
    Not,

    // Operators
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Caret,
    DotDot,
    Hash,

    // Comparison
    Equal,
    NotEqual,
    TildeEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,

    // Assignment
    Assign,

    // Delimiters
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    Comma,
    Dot,
    Colon,

    // Special
    Eof,
}

#[derive(Debug, Clone)]
pub struct Token {
    pub kind: TokenKind,
    pub line: usize,
    pub column: usize,
}

impl Token {
    pub fn new(kind: TokenKind, line: usize, column: usize) -> Self {
        Self { kind, line, column }
    }
}

pub struct Lexer {
    source: Vec<char>,
    pos: usize,
    line: usize,
    column: usize,
}

impl Lexer {
    pub fn new(source: &str) -> Self {
        Self {
            source: source.chars().collect(),
            pos: 0,
            line: 1,
            column: 1,
        }
    }

    pub fn tokenize(&mut self) -> GroveResult<Vec<Token>> {
        let mut tokens = Vec::new();
        loop {
            let tok = self.next_token()?;
            let is_eof = tok.kind == TokenKind::Eof;
            tokens.push(tok);
            if is_eof { break; }
        }
        Ok(tokens)
    }

    fn peek(&self) -> char {
        if self.pos < self.source.len() {
            self.source[self.pos]
        } else {
            '\0'
        }
    }

    fn peek_next(&self) -> char {
        if self.pos + 1 < self.source.len() {
            self.source[self.pos + 1]
        } else {
            '\0'
        }
    }

    fn advance(&mut self) -> char {
        let ch = self.peek();
        self.pos += 1;
        if ch == '\n' {
            self.line += 1;
            self.column = 1;
        } else {
            self.column += 1;
        }
        ch
    }

    fn skip_whitespace_and_comments(&mut self) {
        loop {
            // Skip whitespace
            while self.pos < self.source.len() && self.peek().is_ascii_whitespace() {
                self.advance();
            }
            // Skip single-line comments: --
            if self.peek() == '-' && self.peek_next() == '-' {
                while self.pos < self.source.len() && self.peek() != '\n' {
                    self.advance();
                }
            } else {
                break;
            }
        }
    }

    fn next_token(&mut self) -> GroveResult<Token> {
        self.skip_whitespace_and_comments();

        let line = self.line;
        let col = self.column;

        if self.pos >= self.source.len() {
            return Ok(Token::new(TokenKind::Eof, line, col));
        }

        let ch = self.peek();

        // Numbers
        if ch.is_ascii_digit() {
            return self.read_number(line, col);
        }

        // Strings
        if ch == '"' || ch == '\'' {
            return self.read_string(line, col);
        }

        // Identifiers and keywords
        if ch.is_ascii_alphabetic() || ch == '_' {
            return self.read_identifier(line, col);
        }

        // Operators and punctuation
        self.advance();
        match ch {
            '+' => Ok(Token::new(TokenKind::Plus, line, col)),
            '*' => Ok(Token::new(TokenKind::Star, line, col)),
            '/' => Ok(Token::new(TokenKind::Slash, line, col)),
            '%' => Ok(Token::new(TokenKind::Percent, line, col)),
            '^' => Ok(Token::new(TokenKind::Caret, line, col)),
            '#' => Ok(Token::new(TokenKind::Hash, line, col)),
            '(' => Ok(Token::new(TokenKind::LeftParen, line, col)),
            ')' => Ok(Token::new(TokenKind::RightParen, line, col)),
            '[' => Ok(Token::new(TokenKind::LeftBracket, line, col)),
            ']' => Ok(Token::new(TokenKind::RightBracket, line, col)),
            '{' => Ok(Token::new(TokenKind::LeftBrace, line, col)),
            '}' => Ok(Token::new(TokenKind::RightBrace, line, col)),
            ',' => Ok(Token::new(TokenKind::Comma, line, col)),
            ':' => Ok(Token::new(TokenKind::Colon, line, col)),
            '-' => Ok(Token::new(TokenKind::Minus, line, col)),
            '.' => {
                if self.peek() == '.' {
                    self.advance();
                    Ok(Token::new(TokenKind::DotDot, line, col))
                } else {
                    Ok(Token::new(TokenKind::Dot, line, col))
                }
            }
            '=' => {
                if self.peek() == '=' {
                    self.advance();
                    Ok(Token::new(TokenKind::Equal, line, col))
                } else {
                    Ok(Token::new(TokenKind::Assign, line, col))
                }
            }
            '~' => {
                if self.peek() == '=' {
                    self.advance();
                    Ok(Token::new(TokenKind::TildeEqual, line, col))
                } else {
                    Err(GroveError::syntax(
                        format!("unexpected character '~'"),
                        line, col,
                    ))
                }
            }
            '!' => {
                if self.peek() == '=' {
                    self.advance();
                    Ok(Token::new(TokenKind::NotEqual, line, col))
                } else {
                    Err(GroveError::syntax(
                        format!("unexpected character '!'"),
                        line, col,
                    ))
                }
            }
            '<' => {
                if self.peek() == '=' {
                    self.advance();
                    Ok(Token::new(TokenKind::LessEqual, line, col))
                } else {
                    Ok(Token::new(TokenKind::Less, line, col))
                }
            }
            '>' => {
                if self.peek() == '=' {
                    self.advance();
                    Ok(Token::new(TokenKind::GreaterEqual, line, col))
                } else {
                    Ok(Token::new(TokenKind::Greater, line, col))
                }
            }
            _ => Err(GroveError::syntax(
                format!("unexpected character '{}'", ch),
                line, col,
            )),
        }
    }

    fn read_number(&mut self, line: usize, col: usize) -> GroveResult<Token> {
        let start = self.pos;
        while self.pos < self.source.len() && self.peek().is_ascii_digit() {
            self.advance();
        }
        if self.peek() == '.' && self.peek_next().is_ascii_digit() {
            self.advance(); // consume '.'
            while self.pos < self.source.len() && self.peek().is_ascii_digit() {
                self.advance();
            }
        }
        let text: String = self.source[start..self.pos].iter().collect();
        let value: f64 = text.parse().map_err(|_| {
            GroveError::syntax(format!("invalid number '{}'", text), line, col)
        })?;
        Ok(Token::new(TokenKind::Number(value), line, col))
    }

    fn read_string(&mut self, line: usize, col: usize) -> GroveResult<Token> {
        let quote = self.advance(); // consume opening quote
        let mut s = String::new();
        loop {
            if self.pos >= self.source.len() {
                return Err(GroveError::syntax("unterminated string", line, col));
            }
            let ch = self.advance();
            if ch == quote {
                break;
            }
            if ch == '\\' {
                if self.pos >= self.source.len() {
                    return Err(GroveError::syntax("unterminated string escape", line, col));
                }
                let esc = self.advance();
                match esc {
                    'n' => s.push('\n'),
                    't' => s.push('\t'),
                    '\\' => s.push('\\'),
                    '\'' => s.push('\''),
                    '"' => s.push('"'),
                    _ => {
                        s.push('\\');
                        s.push(esc);
                    }
                }
            } else {
                s.push(ch);
            }
        }
        Ok(Token::new(TokenKind::StringLit(s), line, col))
    }

    fn read_identifier(&mut self, line: usize, col: usize) -> GroveResult<Token> {
        let start = self.pos;
        while self.pos < self.source.len()
            && (self.peek().is_ascii_alphanumeric() || self.peek() == '_')
        {
            self.advance();
        }
        let text: String = self.source[start..self.pos].iter().collect();
        let kind = match text.as_str() {
            "local" => TokenKind::Local,
            "let" => TokenKind::Let,
            "fn" => TokenKind::Fn,
            "blueprint" => TokenKind::Blueprint,
            "build" => TokenKind::Build,
            "end" => TokenKind::End,
            "if" => TokenKind::If,
            "then" => TokenKind::Then,
            "elseif" => TokenKind::ElseIf,
            "else" => TokenKind::Else,
            "for" => TokenKind::For,
            "in" => TokenKind::In,
            "do" => TokenKind::Do,
            "while" => TokenKind::While,
            "repeat" => TokenKind::Repeat,
            "until" => TokenKind::Until,
            "return" => TokenKind::Return,
            "break" => TokenKind::Break,
            "continue" => TokenKind::Continue,
            "and" => TokenKind::And,
            "or" => TokenKind::Or,
            "not" => TokenKind::Not,
            "true" => TokenKind::True,
            "false" => TokenKind::False,
            "nil" => TokenKind::Nil,
            _ => TokenKind::Identifier(text),
        };
        Ok(Token::new(kind, line, col))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_tokens() {
        let mut lex = Lexer::new("local x = 42");
        let tokens = lex.tokenize().unwrap();
        assert_eq!(tokens.len(), 5); // local, x, =, 42, EOF
        assert_eq!(tokens[0].kind, TokenKind::Local);
        assert!(matches!(&tokens[1].kind, TokenKind::Identifier(s) if s == "x"));
        assert_eq!(tokens[2].kind, TokenKind::Assign);
        assert!(matches!(tokens[3].kind, TokenKind::Number(n) if n == 42.0));
        assert_eq!(tokens[4].kind, TokenKind::Eof);
    }

    #[test]
    fn test_string_literals() {
        let mut lex = Lexer::new(r#""hello world" 'test'"#);
        let tokens = lex.tokenize().unwrap();
        assert!(matches!(&tokens[0].kind, TokenKind::StringLit(s) if s == "hello world"));
        assert!(matches!(&tokens[1].kind, TokenKind::StringLit(s) if s == "test"));
    }

    #[test]
    fn test_comments() {
        let mut lex = Lexer::new("-- this is a comment\nlocal x = 1");
        let tokens = lex.tokenize().unwrap();
        assert_eq!(tokens[0].kind, TokenKind::Local);
    }

    #[test]
    fn test_operators() {
        let mut lex = Lexer::new("+ - * / % ^ .. == ~= != < <= > >=");
        let tokens = lex.tokenize().unwrap();
        assert_eq!(tokens[0].kind, TokenKind::Plus);
        assert_eq!(tokens[1].kind, TokenKind::Minus);
        assert_eq!(tokens[2].kind, TokenKind::Star);
        assert_eq!(tokens[3].kind, TokenKind::Slash);
        assert_eq!(tokens[4].kind, TokenKind::Percent);
        assert_eq!(tokens[5].kind, TokenKind::Caret);
        assert_eq!(tokens[6].kind, TokenKind::DotDot);
        assert_eq!(tokens[7].kind, TokenKind::Equal);
        assert_eq!(tokens[8].kind, TokenKind::TildeEqual);
        assert_eq!(tokens[9].kind, TokenKind::NotEqual);
        assert_eq!(tokens[10].kind, TokenKind::Less);
        assert_eq!(tokens[11].kind, TokenKind::LessEqual);
        assert_eq!(tokens[12].kind, TokenKind::Greater);
        assert_eq!(tokens[13].kind, TokenKind::GreaterEqual);
    }

    #[test]
    fn test_keywords() {
        let mut lex = Lexer::new("if then else elseif end while do for in blueprint build");
        let tokens = lex.tokenize().unwrap();
        assert_eq!(tokens[0].kind, TokenKind::If);
        assert_eq!(tokens[1].kind, TokenKind::Then);
        assert_eq!(tokens[2].kind, TokenKind::Else);
        assert_eq!(tokens[3].kind, TokenKind::ElseIf);
        assert_eq!(tokens[4].kind, TokenKind::End);
        assert_eq!(tokens[5].kind, TokenKind::While);
        assert_eq!(tokens[6].kind, TokenKind::Do);
        assert_eq!(tokens[7].kind, TokenKind::For);
        assert_eq!(tokens[8].kind, TokenKind::In);
        assert_eq!(tokens[9].kind, TokenKind::Blueprint);
        assert_eq!(tokens[10].kind, TokenKind::Build);
    }

    #[test]
    fn test_line_tracking() {
        let mut lex = Lexer::new("x\ny\nz");
        let tokens = lex.tokenize().unwrap();
        assert_eq!(tokens[0].line, 1);
        assert_eq!(tokens[1].line, 2);
        assert_eq!(tokens[2].line, 3);
    }

    #[test]
    fn test_float_number() {
        let mut lex = Lexer::new("3.14");
        let tokens = lex.tokenize().unwrap();
        assert!(matches!(tokens[0].kind, TokenKind::Number(n) if (n - 3.14).abs() < 1e-10));
    }
}
