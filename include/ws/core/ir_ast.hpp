#pragma once

// Standard library
#include <string>
#include <vector>
#include <memory>
#include <variant>

namespace ws::ir {

// =============================================================================
// IR Types
// =============================================================================

// Data types supported in the intermediate representation.
enum class Type { F32, F64, I32, U32, Bool, Vec2, Vec3, Unknown };

// =============================================================================
// Unary Operations
// =============================================================================

// Unary arithmetic and trigonometric operations.
enum class UnaryOp { 
    Abs, Neg, Not, Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh, 
    Sqrt, Cbrt, Exp, Log, Log10, Log2, Floor, Ceil, Round, Frac 
};

// =============================================================================
// Binary Operations
// =============================================================================

// Binary arithmetic, logical, and comparison operations.
enum class BinaryOp { 
    Add, Sub, Mul, Div, Mod, Pow, Min, Max, 
    And, Or, Equal, NotEqual, LessThan, LessEqual, GreaterThan, GreaterEqual 
};

// =============================================================================
// Expression Base
// =============================================================================

// Base class for all IR expressions.
struct Expr {
    virtual ~Expr() = default;
};

// =============================================================================
// Literal Expression
// =============================================================================

// A literal constant value.
struct LiteralExpr : public Expr {
    Type type;
    std::variant<float, double, int, unsigned int, bool> value;
};

// =============================================================================
// Constant Expression
// =============================================================================

// A named constant value.
struct ConstantExpr : public Expr {
    Type type;
    std::variant<float, double, int, unsigned int, bool> value;
};

// =============================================================================
// Variable Reference Expression
// =============================================================================

// Reference to a variable by name (prefixed with %).
struct VarRefExpr : public Expr {
    std::string name; // "%var"
};

// =============================================================================
// Load Expression
// =============================================================================

// Loads a value from a field with optional offset.
struct LoadExpr : public Expr {
    std::string field;
    int x_offset{0};
    int y_offset{0};
};

// =============================================================================
// Global Load Expression
// =============================================================================

// Loads a global field value.
struct GlobalLoadExpr : public Expr {
    std::string field_name;
};

// =============================================================================
// Unary Expression
// =============================================================================

// Expression with a single operand and unary operator.
struct UnaryExpr : public Expr {
    UnaryOp op;
    std::unique_ptr<Expr> operand;
};

// =============================================================================
// Binary Expression
// =============================================================================

// Expression with two operands and binary operator.
struct BinaryExpr : public Expr {
    BinaryOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

// =============================================================================
// Select Expression
// =============================================================================

// Conditional expression (ternary operator).
struct SelectExpr : public Expr {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> true_expr;
    std::unique_ptr<Expr> false_expr;
};

// =============================================================================
// Cast Expression
// =============================================================================

// Type conversion expression.
struct CastExpr : public Expr {
    std::unique_ptr<Expr> expr;
    Type target_type;
};

// =============================================================================
// Clamp Expression
// =============================================================================

// Clamps a value between min and max bounds.
struct ClampExpr : public Expr {
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Expr> min_expr;
    std::unique_ptr<Expr> max_expr;
};

// =============================================================================
// Extract Expression
// =============================================================================

// Extracts a component from a vector type.
struct ExtractExpr : public Expr {
    std::unique_ptr<Expr> expr;
    int index{0};
};

// =============================================================================
// Laplacian Expression
// =============================================================================

// Computes the Laplacian (second derivative) of a field.
struct LaplacianExpr : public Expr {
    std::string field_name;
};

// =============================================================================
// Gradient Expression
// =============================================================================

// Computes the gradient of a field in a specific direction.
struct GradientExpr : public Expr {
    std::string field_name;
    int direction{0}; 
};

// =============================================================================
// Statement Base
// =============================================================================

// Base class for all IR statements.
struct Stmt {
    virtual ~Stmt() = default;
};

// =============================================================================
// Variable Declaration Statement
// =============================================================================

// Declares a new variable with an initializer.
struct VarDeclStmt : public Stmt {
    Type type;
    std::string identifier;
    std::unique_ptr<Expr> expr;
};

// =============================================================================
// Assignment Statement
// =============================================================================

// Assigns a value to a field with optional offset.
struct AssignStmt : public Stmt {
    std::string field;
    int offset{0};
    std::unique_ptr<Expr> expr;
};

// =============================================================================
// Return Statement
// =============================================================================

// Returns a value from an interaction.
struct ReturnStmt : public Stmt {
    std::unique_ptr<Expr> expr;
};

// =============================================================================
// Interaction Declaration
// =============================================================================

// A named interaction containing statements.
struct InteractionDecl {
    std::string id;
    std::vector<std::unique_ptr<Stmt>> stmts;
};

// =============================================================================
// Global Declaration
// =============================================================================

// A global variable declaration with a literal value.
struct GlobalDecl {
    Type type;
    std::string identifier;
    std::unique_ptr<Expr> literal_expr;
};

// =============================================================================
// Program
// =============================================================================

// Complete IR program containing globals and interactions.
struct Program {
    std::vector<std::unique_ptr<GlobalDecl>> globals;
    std::vector<std::unique_ptr<InteractionDecl>> interactions;
};

// Parser main entry point forward declaration
Program parse_ir(const std::string& source);

} // namespace ws::ir
