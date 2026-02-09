use std::collections::HashMap;
use crate::types::Value;

#[derive(Debug)]
pub struct Environment {
    scopes: Vec<HashMap<String, Value>>,
}

impl Environment {
    pub fn new() -> Self {
        Self {
            scopes: vec![HashMap::new()], // global scope
        }
    }

    pub fn push_scope(&mut self) {
        self.scopes.push(HashMap::new());
    }

    pub fn pop_scope(&mut self) {
        if self.scopes.len() > 1 {
            self.scopes.pop();
        }
    }

    /// Define a new variable in the current (innermost) scope.
    pub fn define(&mut self, name: &str, value: Value) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.insert(name.to_string(), value);
        }
    }

    /// Set an existing variable, walking up the scope chain.
    /// Returns false if the variable doesn't exist in any scope.
    pub fn set(&mut self, name: &str, value: Value) -> bool {
        for scope in self.scopes.iter_mut().rev() {
            if scope.contains_key(name) {
                scope.insert(name.to_string(), value);
                return true;
            }
        }
        false
    }

    /// Get a variable's value, walking up the scope chain.
    pub fn get(&self, name: &str) -> Option<&Value> {
        for scope in self.scopes.iter().rev() {
            if let Some(val) = scope.get(name) {
                return Some(val);
            }
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_define_and_get() {
        let mut env = Environment::new();
        env.define("x", Value::Number(42.0));
        assert_eq!(env.get("x"), Some(&Value::Number(42.0)));
    }

    #[test]
    fn test_scope_chain() {
        let mut env = Environment::new();
        env.define("x", Value::Number(1.0));
        env.push_scope();
        env.define("y", Value::Number(2.0));
        assert_eq!(env.get("x"), Some(&Value::Number(1.0)));
        assert_eq!(env.get("y"), Some(&Value::Number(2.0)));
        env.pop_scope();
        assert_eq!(env.get("x"), Some(&Value::Number(1.0)));
        assert_eq!(env.get("y"), None);
    }

    #[test]
    fn test_set_existing() {
        let mut env = Environment::new();
        env.define("x", Value::Number(1.0));
        env.push_scope();
        assert!(env.set("x", Value::Number(99.0)));
        assert_eq!(env.get("x"), Some(&Value::Number(99.0)));
    }

    #[test]
    fn test_set_nonexistent() {
        let mut env = Environment::new();
        assert!(!env.set("x", Value::Number(1.0)));
    }

    #[test]
    fn test_shadow() {
        let mut env = Environment::new();
        env.define("x", Value::Number(1.0));
        env.push_scope();
        env.define("x", Value::Number(2.0));
        assert_eq!(env.get("x"), Some(&Value::Number(2.0)));
        env.pop_scope();
        assert_eq!(env.get("x"), Some(&Value::Number(1.0)));
    }
}
