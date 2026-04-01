#pragma once

#include <string>
#include <vector>
#include <memory>
#include <variant>

namespace ws::ir {

enum class Type { F32, F64, I32, U32, Bool, Vec2, Vec3, Unknown };

enum class UnaryOp { 
    Abs, Neg, Not, Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh, 
    Sqrt, Cbrt, Exp, Log, Log10, Log2, Floor, Ceil, Round, Frac 
};

enum class BinaryOp { 
    Add, Sub, Mul, Div, Mod, Pow, Min, Max, 
    And, Or, Equal, NotEqual, LessThan, LessEqual, GreaterThan, GreaterEqual 
};

struct Expr {
    virtual ~Expr() = default;
};

struct LiteralExpr : public Expr {
    Type type;
    std::variant<float, double, int, unsigned int, bool> value;
};

struct ConstantExpr : public Expr {
    Type type;
    std::variant<float, double, int, unsigned int, bool> value;
};

struct VarRefExpr : public Expr {
    std::string name; // "%var"
};

struct LoadExpr : public Expr {
    std::string field;
    int x_offset{0};
    int y_offset{0};
};

struct GlobalLoadExpr : public Expr {
    std::string field_name;
};

struct UnaryExpr : public Expr {
    UnaryOp op;
    std::unique_ptr<Expr> operand;
};

struct BinaryExpr : public Expr {
    BinaryOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct SelectExpr : public Expr {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> true_expr;
    std::unique_ptr<Expr> false_expr;
};

struct CastExpr : public Expr {
    std::unique_ptr<Expr> expr;
    Type target_type;
};

struct ClampExpr : public Expr {
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Expr> min_expr;
    std::unique_ptr<Expr> max_expr;
};

struct ExtractExpr : public Expr {
    std::unique_ptr<Expr> expr;
    int index{0};
};

struct LaplacianExpr : public Expr {
    std::string field_name;
};

struct GradientExpr : public Expr {
    std::string field_name;
    int direction{0}; 
};

struct Stmt {
    virtual ~Stmt() = default;
};

struct VarDeclStmt : public Stmt {
    Type type;
    std::string identifier;
    std::unique_ptr<Expr> expr;
};

struct AssignStmt : public Stmt {
    std::string field;
    int offset{0};
    std::unique_ptr<Expr> expr;
};

struct ReturnStmt : public Stmt {
    std::unique_ptr<Expr> expr;
};

struct InteractionDecl {
    std::string id;
    std::vector<std::unique_ptr<Stmt>> stmts;
};

struct GlobalDecl {
    Type type;
    std::string identifier;
    std::unique_ptr<Expr> literal_expr;
};

struct Program {
    std::vector<std::unique_ptr<GlobalDecl>> globals;
    std::vector<std::unique_ptr<InteractionDecl>> interactions;
};

// Parser main entry point forward declaration
Program parse_ir(const std::string& source);

} // namespace ws::ir
