use crate::ast::*;
use crate::error::{GroveError, GroveResult};
use crate::lexer::{Token, TokenKind};

pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Self { tokens, pos: 0 }
    }

    pub fn parse(&mut self) -> GroveResult<Program> {
        let mut statements = Vec::new();
        while !self.is_at_end() {
            statements.push(self.statement()?);
        }
        Ok(Program { statements })
    }

    // ── Helpers ──────────────────────────────────────────

    fn peek(&self) -> &TokenKind {
        &self.tokens[self.pos].kind
    }

    fn current_token(&self) -> &Token {
        &self.tokens[self.pos]
    }

    fn is_at_end(&self) -> bool {
        matches!(self.peek(), TokenKind::Eof)
    }

    fn advance(&mut self) -> &Token {
        let tok = &self.tokens[self.pos];
        if !self.is_at_end() {
            self.pos += 1;
        }
        tok
    }

    fn check(&self, kind: &TokenKind) -> bool {
        std::mem::discriminant(self.peek()) == std::mem::discriminant(kind)
    }

    fn expect(&mut self, expected: &TokenKind) -> GroveResult<&Token> {
        if self.check(expected) {
            Ok(self.advance())
        } else {
            let tok = self.current_token();
            Err(GroveError::syntax(
                format!("expected {:?}, got {:?}", expected, tok.kind),
                tok.line,
                tok.column,
            ))
        }
    }

    fn span(&self) -> Span {
        let tok = self.current_token();
        Span { line: tok.line, column: tok.column }
    }

    #[allow(dead_code)]
    fn prev_span(&self) -> Span {
        let tok = if self.pos > 0 { &self.tokens[self.pos - 1] } else { &self.tokens[0] };
        Span { line: tok.line, column: tok.column }
    }

    // ── Statements ──────────────────────────────────────

    fn statement(&mut self) -> GroveResult<Stmt> {
        match self.peek() {
            TokenKind::Local | TokenKind::Let => self.local_decl(),
            TokenKind::If => self.if_stmt(),
            TokenKind::While => self.while_stmt(),
            TokenKind::For => self.for_stmt(),
            TokenKind::Repeat => self.repeat_until(),
            TokenKind::Blueprint | TokenKind::Fn => self.blueprint_stmt(),
            TokenKind::Build => self.build_stmt(),
            TokenKind::Return => self.return_stmt(),
            TokenKind::Break => { let s = self.span(); self.advance(); Ok(Stmt::Break { span: s }) }
            TokenKind::Continue => { let s = self.span(); self.advance(); Ok(Stmt::Continue { span: s }) }
            _ => self.expr_or_assign_stmt(),
        }
    }

    fn local_decl(&mut self) -> GroveResult<Stmt> {
        let s = self.span();
        self.advance(); // consume 'local' or 'let'
        let name = self.expect_identifier()?;
        let init = if matches!(self.peek(), TokenKind::Assign) {
            self.advance();
            Some(self.expression(0)?)
        } else {
            None
        };
        Ok(Stmt::LocalDecl { name, init, span: s })
    }

    fn if_stmt(&mut self) -> GroveResult<Stmt> {
        let s = self.span();
        self.advance(); // consume 'if'
        let condition = self.expression(0)?;
        self.expect(&TokenKind::Then)?;

        let then_body = self.block_until(&[
            TokenKind::ElseIf,
            TokenKind::Else,
            TokenKind::End,
        ])?;

        let mut elseif_clauses = Vec::new();
        while matches!(self.peek(), TokenKind::ElseIf) {
            self.advance();
            let cond = self.expression(0)?;
            self.expect(&TokenKind::Then)?;
            let body = self.block_until(&[
                TokenKind::ElseIf,
                TokenKind::Else,
                TokenKind::End,
            ])?;
            elseif_clauses.push((cond, body));
        }

        let else_body = if matches!(self.peek(), TokenKind::Else) {
            self.advance();
            Some(self.block_until(&[TokenKind::End])?)
        } else {
            None
        };

        self.expect(&TokenKind::End)?;
        Ok(Stmt::If { condition, then_body, elseif_clauses, else_body, span: s })
    }

    fn while_stmt(&mut self) -> GroveResult<Stmt> {
        let s = self.span();
        self.advance(); // consume 'while'
        let condition = self.expression(0)?;
        self.expect(&TokenKind::Do)?;
        let body = self.block_until(&[TokenKind::End])?;
        self.expect(&TokenKind::End)?;
        Ok(Stmt::While { condition, body, span: s })
    }

    fn for_stmt(&mut self) -> GroveResult<Stmt> {
        let s = self.span();
        self.advance(); // consume 'for'
        let first_var = self.expect_identifier()?;

        if matches!(self.peek(), TokenKind::Assign) {
            // Numeric for: for i = start, limit [, step] do ... end
            self.advance(); // consume '='
            let start = self.expression(0)?;
            self.expect(&TokenKind::Comma)?;
            let limit = self.expression(0)?;
            let step = if matches!(self.peek(), TokenKind::Comma) {
                self.advance();
                Some(self.expression(0)?)
            } else {
                None
            };
            self.expect(&TokenKind::Do)?;
            let body = self.block_until(&[TokenKind::End])?;
            self.expect(&TokenKind::End)?;
            Ok(Stmt::NumericFor { var: first_var, start, limit, step, body, span: s })
        } else {
            // Generic for: for k, v in expr do ... end
            let mut vars = vec![first_var];
            while matches!(self.peek(), TokenKind::Comma) {
                self.advance();
                vars.push(self.expect_identifier()?);
            }
            self.expect(&TokenKind::In)?;
            let iter = self.expression(0)?;
            self.expect(&TokenKind::Do)?;
            let body = self.block_until(&[TokenKind::End])?;
            self.expect(&TokenKind::End)?;
            Ok(Stmt::GenericFor { vars, iter, body, span: s })
        }
    }

    fn repeat_until(&mut self) -> GroveResult<Stmt> {
        let s = self.span();
        self.advance(); // consume 'repeat'
        let body = self.block_until(&[TokenKind::Until])?;
        self.expect(&TokenKind::Until)?;
        let condition = self.expression(0)?;
        Ok(Stmt::RepeatUntil { body, condition, span: s })
    }

    fn blueprint_stmt(&mut self) -> GroveResult<Stmt> {
        let s = self.span();
        self.advance(); // consume 'blueprint' or 'fn'
        let name = self.expect_identifier()?;
        self.expect(&TokenKind::LeftParen)?;
        let params = self.param_list()?;
        self.expect(&TokenKind::RightParen)?;
        let body = self.block_until(&[TokenKind::End])?;
        self.expect(&TokenKind::End)?;
        Ok(Stmt::Blueprint { name, params, body, span: s })
    }

    fn build_stmt(&mut self) -> GroveResult<Stmt> {
        let s = self.span();
        self.advance(); // consume 'build'
        let name = self.expect_identifier()?;
        self.expect(&TokenKind::LeftParen)?;
        let args = self.arg_list()?;
        self.expect(&TokenKind::RightParen)?;
        Ok(Stmt::Build { name, args, span: s })
    }

    fn return_stmt(&mut self) -> GroveResult<Stmt> {
        let s = self.span();
        self.advance(); // consume 'return'
        // Return has optional value — if next token could start an expression, parse it
        let value = if self.is_at_end() || self.is_block_terminator() {
            None
        } else {
            Some(self.expression(0)?)
        };
        Ok(Stmt::Return { value, span: s })
    }

    fn expr_or_assign_stmt(&mut self) -> GroveResult<Stmt> {
        let s = self.span();
        let expr = self.expression(0)?;

        if matches!(self.peek(), TokenKind::Assign) {
            self.advance();
            let value = self.expression(0)?;
            Ok(Stmt::Assign { target: expr, value, span: s })
        } else {
            Ok(Stmt::ExprStmt { expr, span: s })
        }
    }

    fn block_until(&mut self, terminators: &[TokenKind]) -> GroveResult<Vec<Stmt>> {
        let mut stmts = Vec::new();
        while !self.is_at_end() && !terminators.iter().any(|t| self.check(t)) {
            stmts.push(self.statement()?);
        }
        if self.is_at_end() && !terminators.iter().any(|t| self.check(t)) {
            return Err(GroveError::syntax(
                format!("unexpected end of input, expected one of {:?}", terminators),
                self.current_token().line,
                self.current_token().column,
            ));
        }
        Ok(stmts)
    }

    fn is_block_terminator(&self) -> bool {
        matches!(
            self.peek(),
            TokenKind::End | TokenKind::Else | TokenKind::ElseIf | TokenKind::Until
        )
    }

    fn expect_identifier(&mut self) -> GroveResult<String> {
        let tok = self.current_token();
        if let TokenKind::Identifier(name) = &tok.kind {
            let name = name.clone();
            self.advance();
            Ok(name)
        } else {
            Err(GroveError::syntax(
                format!("expected identifier, got {:?}", tok.kind),
                tok.line,
                tok.column,
            ))
        }
    }

    fn param_list(&mut self) -> GroveResult<Vec<String>> {
        let mut params = Vec::new();
        if !matches!(self.peek(), TokenKind::RightParen) {
            params.push(self.expect_identifier()?);
            while matches!(self.peek(), TokenKind::Comma) {
                self.advance();
                params.push(self.expect_identifier()?);
            }
        }
        Ok(params)
    }

    fn arg_list(&mut self) -> GroveResult<Vec<Expr>> {
        let mut args = Vec::new();
        if !matches!(self.peek(), TokenKind::RightParen) {
            args.push(self.expression(0)?);
            while matches!(self.peek(), TokenKind::Comma) {
                self.advance();
                args.push(self.expression(0)?);
            }
        }
        Ok(args)
    }

    // ── Pratt Expression Parser ─────────────────────────

    fn expression(&mut self, min_bp: u8) -> GroveResult<Expr> {
        let mut left = self.prefix()?;

        loop {
            // Check for postfix operators first (call, field, index, method)
            match self.peek() {
                TokenKind::LeftParen => {
                    let s = self.span();
                    self.advance();
                    let args = self.arg_list()?;
                    self.expect(&TokenKind::RightParen)?;
                    left = Expr::Call { callee: Box::new(left), args, span: s };
                    continue;
                }
                TokenKind::Dot => {
                    let s = self.span();
                    self.advance();
                    let field = self.expect_identifier()?;
                    left = Expr::FieldAccess { object: Box::new(left), field, span: s };
                    continue;
                }
                TokenKind::LeftBracket => {
                    let s = self.span();
                    self.advance();
                    let index = self.expression(0)?;
                    self.expect(&TokenKind::RightBracket)?;
                    left = Expr::IndexAccess { object: Box::new(left), index: Box::new(index), span: s };
                    continue;
                }
                TokenKind::Colon => {
                    let s = self.span();
                    self.advance();
                    let method = self.expect_identifier()?;
                    self.expect(&TokenKind::LeftParen)?;
                    let args = self.arg_list()?;
                    self.expect(&TokenKind::RightParen)?;
                    left = Expr::MethodCall { object: Box::new(left), method, args, span: s };
                    continue;
                }
                _ => {}
            }

            // Check for infix operators
            let Some((op, left_bp, right_bp)) = self.infix_binding_power() else {
                break;
            };

            if left_bp < min_bp {
                break;
            }

            self.advance(); // consume operator token
            let right = self.expression(right_bp)?;
            let s = left.span().clone();
            left = Expr::BinaryOp {
                left: Box::new(left),
                op,
                right: Box::new(right),
                span: s,
            };
        }

        Ok(left)
    }

    fn prefix(&mut self) -> GroveResult<Expr> {
        let tok = self.current_token();
        let s = Span { line: tok.line, column: tok.column };

        match &tok.kind {
            TokenKind::Number(n) => {
                let v = *n;
                self.advance();
                Ok(Expr::NumberLit { value: v, span: s })
            }
            TokenKind::StringLit(val) => {
                let v = val.clone();
                self.advance();
                Ok(Expr::StringLit { value: v, span: s })
            }
            TokenKind::True => {
                self.advance();
                Ok(Expr::BoolLit { value: true, span: s })
            }
            TokenKind::False => {
                self.advance();
                Ok(Expr::BoolLit { value: false, span: s })
            }
            TokenKind::Nil => {
                self.advance();
                Ok(Expr::NilLit { span: s })
            }
            TokenKind::Identifier(_) => {
                let name = self.expect_identifier()?;
                Ok(Expr::Ident { name, span: s })
            }
            TokenKind::Minus => {
                self.advance();
                let operand = self.expression(self.unary_bp())?;
                Ok(Expr::UnaryOp { op: UnaryOp::Neg, operand: Box::new(operand), span: s })
            }
            TokenKind::Not => {
                self.advance();
                let operand = self.expression(self.unary_bp())?;
                Ok(Expr::UnaryOp { op: UnaryOp::Not, operand: Box::new(operand), span: s })
            }
            TokenKind::Hash => {
                self.advance();
                let operand = self.expression(self.unary_bp())?;
                Ok(Expr::UnaryOp { op: UnaryOp::Len, operand: Box::new(operand), span: s })
            }
            TokenKind::LeftParen => {
                self.advance();
                let expr = self.expression(0)?;
                self.expect(&TokenKind::RightParen)?;
                Ok(expr)
            }
            TokenKind::LeftBracket => {
                self.advance();
                let mut elements = Vec::new();
                if !matches!(self.peek(), TokenKind::RightBracket) {
                    elements.push(self.expression(0)?);
                    while matches!(self.peek(), TokenKind::Comma) {
                        self.advance();
                        if matches!(self.peek(), TokenKind::RightBracket) {
                            break; // trailing comma
                        }
                        elements.push(self.expression(0)?);
                    }
                }
                self.expect(&TokenKind::RightBracket)?;
                Ok(Expr::ArrayLit { elements, span: s })
            }
            TokenKind::LeftBrace => {
                self.advance();
                let mut fields = Vec::new();
                if !matches!(self.peek(), TokenKind::RightBrace) {
                    let key = self.expect_identifier()?;
                    self.expect(&TokenKind::Assign)?;
                    let val = self.expression(0)?;
                    fields.push((key, val));
                    while matches!(self.peek(), TokenKind::Comma) {
                        self.advance();
                        if matches!(self.peek(), TokenKind::RightBrace) {
                            break; // trailing comma
                        }
                        let key = self.expect_identifier()?;
                        self.expect(&TokenKind::Assign)?;
                        let val = self.expression(0)?;
                        fields.push((key, val));
                    }
                }
                self.expect(&TokenKind::RightBrace)?;
                Ok(Expr::TableLit { fields, span: s })
            }
            _ => {
                Err(GroveError::syntax(
                    format!("unexpected token {:?}", tok.kind),
                    tok.line,
                    tok.column,
                ))
            }
        }
    }

    fn unary_bp(&self) -> u8 {
        13 // Unary binds tighter than binary except power
    }

    /// Returns (BinOp, left_bp, right_bp) for the current token if it's an infix operator.
    fn infix_binding_power(&self) -> Option<(BinOp, u8, u8)> {
        match self.peek() {
            TokenKind::Or => Some((BinOp::Or, 1, 2)),
            TokenKind::And => Some((BinOp::And, 3, 4)),
            TokenKind::Equal => Some((BinOp::Eq, 5, 6)),
            TokenKind::NotEqual | TokenKind::TildeEqual => Some((BinOp::NotEq, 5, 6)),
            TokenKind::Less => Some((BinOp::Lt, 5, 6)),
            TokenKind::LessEqual => Some((BinOp::LtEq, 5, 6)),
            TokenKind::Greater => Some((BinOp::Gt, 5, 6)),
            TokenKind::GreaterEqual => Some((BinOp::GtEq, 5, 6)),
            TokenKind::DotDot => Some((BinOp::Concat, 7, 8)),
            TokenKind::Plus => Some((BinOp::Add, 9, 10)),
            TokenKind::Minus => Some((BinOp::Sub, 9, 10)),
            TokenKind::Star => Some((BinOp::Mul, 11, 12)),
            TokenKind::Slash => Some((BinOp::Div, 11, 12)),
            TokenKind::Percent => Some((BinOp::Mod, 11, 12)),
            // Power is right-associative: left_bp > right_bp would be left-assoc,
            // so we use right_bp > left_bp for right-assoc
            TokenKind::Caret => Some((BinOp::Pow, 16, 15)),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;

    fn parse_str(src: &str) -> GroveResult<Program> {
        let mut lex = Lexer::new(src);
        let tokens = lex.tokenize()?;
        let mut parser = Parser::new(tokens);
        parser.parse()
    }

    #[test]
    fn test_local_decl() {
        let prog = parse_str("local x = 42").unwrap();
        assert_eq!(prog.statements.len(), 1);
        assert!(matches!(&prog.statements[0], Stmt::LocalDecl { name, .. } if name == "x"));
    }

    #[test]
    fn test_binary_expr() {
        let prog = parse_str("local y = x * 2 + 5").unwrap();
        assert_eq!(prog.statements.len(), 1);
        if let Stmt::LocalDecl { init: Some(expr), .. } = &prog.statements[0] {
            // Should be Add(Mul(x, 2), 5) due to precedence
            assert!(matches!(expr, Expr::BinaryOp { op: BinOp::Add, .. }));
        } else {
            panic!("expected local decl with init");
        }
    }

    #[test]
    fn test_function_call() {
        let prog = parse_str("log(42)").unwrap();
        assert_eq!(prog.statements.len(), 1);
        if let Stmt::ExprStmt { expr, .. } = &prog.statements[0] {
            assert!(matches!(expr, Expr::Call { .. }));
        } else {
            panic!("expected expr stmt");
        }
    }

    #[test]
    fn test_if_stmt() {
        let prog = parse_str("if x > 10 then\n  log(x)\nend").unwrap();
        assert_eq!(prog.statements.len(), 1);
        assert!(matches!(&prog.statements[0], Stmt::If { .. }));
    }

    #[test]
    fn test_while_stmt() {
        let prog = parse_str("while true do\n  log(1)\nend").unwrap();
        assert_eq!(prog.statements.len(), 1);
        assert!(matches!(&prog.statements[0], Stmt::While { .. }));
    }

    #[test]
    fn test_numeric_for() {
        let prog = parse_str("for i = 1, 10 do\n  log(i)\nend").unwrap();
        assert_eq!(prog.statements.len(), 1);
        assert!(matches!(&prog.statements[0], Stmt::NumericFor { .. }));
    }

    #[test]
    fn test_blueprint() {
        let prog = parse_str("blueprint foo(a, b)\n  log(a)\nend").unwrap();
        assert_eq!(prog.statements.len(), 1);
        assert!(matches!(&prog.statements[0], Stmt::Blueprint { name, .. } if name == "foo"));
    }

    #[test]
    fn test_build() {
        let prog = parse_str("build my_house(origin)").unwrap();
        assert_eq!(prog.statements.len(), 1);
        assert!(matches!(&prog.statements[0], Stmt::Build { name, .. } if name == "my_house"));
    }

    #[test]
    fn test_syntax_error() {
        let result = parse_str("local x = 10 +");
        assert!(result.is_err());
    }

    #[test]
    fn test_field_access() {
        let prog = parse_str("local a = obj.x").unwrap();
        if let Stmt::LocalDecl { init: Some(expr), .. } = &prog.statements[0] {
            assert!(matches!(expr, Expr::FieldAccess { field, .. } if field == "x"));
        } else {
            panic!("expected field access");
        }
    }

    #[test]
    fn test_array_literal() {
        let prog = parse_str("local a = [1, 2, 3]").unwrap();
        assert_eq!(prog.statements.len(), 1);
    }

    #[test]
    fn test_table_literal() {
        let prog = parse_str("local t = {name = \"foo\", size = 4}").unwrap();
        assert_eq!(prog.statements.len(), 1);
    }

    #[test]
    fn test_elseif() {
        let prog = parse_str("if x > 10 then\n  log(1)\nelseif x > 5 then\n  log(2)\nelse\n  log(3)\nend").unwrap();
        if let Stmt::If { elseif_clauses, else_body, .. } = &prog.statements[0] {
            assert_eq!(elseif_clauses.len(), 1);
            assert!(else_body.is_some());
        } else {
            panic!("expected if stmt");
        }
    }

    #[test]
    fn test_unary_neg() {
        let prog = parse_str("local x = -5").unwrap();
        if let Stmt::LocalDecl { init: Some(expr), .. } = &prog.statements[0] {
            assert!(matches!(expr, Expr::UnaryOp { op: UnaryOp::Neg, .. }));
        } else {
            panic!("expected unary neg");
        }
    }

    #[test]
    fn test_string_concat() {
        let prog = parse_str(r#"local s = "hello" .. " world""#).unwrap();
        if let Stmt::LocalDecl { init: Some(expr), .. } = &prog.statements[0] {
            assert!(matches!(expr, Expr::BinaryOp { op: BinOp::Concat, .. }));
        } else {
            panic!("expected concat");
        }
    }
}
