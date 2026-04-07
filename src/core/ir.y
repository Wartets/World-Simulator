%{
#include "ws/core/ir_ast.hpp"
#include <iostream>
#include <vector>
#include <string.h>
#include <stdlib.h>

using namespace ws::ir;

extern int yylex();
extern int yylineno;
extern void yyerror(const char* s);

extern ws::ir::Program* g_program;

Type stringToType(const char* str) {
    if (strcmp(str, "f32") == 0) return Type::F32;
    if (strcmp(str, "f64") == 0) return Type::F64;
    if (strcmp(str, "i32") == 0) return Type::I32;
    if (strcmp(str, "u32") == 0) return Type::U32;
    if (strcmp(str, "bool") == 0) return Type::Bool;
    if (strcmp(str, "vec2") == 0) return Type::Vec2;
    if (strcmp(str, "vec3") == 0) return Type::Vec3;
    return Type::Unknown;
}

BinaryOp parseCompareOperator(const char* str) {
    if (str == nullptr) {
        return BinaryOp::Equal;
    }
    if (strcmp(str, "==") == 0) return BinaryOp::Equal;
    if (strcmp(str, "!=") == 0) return BinaryOp::NotEqual;
    if (strcmp(str, "<") == 0) return BinaryOp::LessThan;
    if (strcmp(str, "<=") == 0) return BinaryOp::LessEqual;
    if (strcmp(str, ">") == 0) return BinaryOp::GreaterThan;
    if (strcmp(str, ">=") == 0) return BinaryOp::GreaterEqual;
    return BinaryOp::Equal;
}

static std::string stripPercentPrefix(const char* text) {
    if (text == nullptr) {
        return {};
    }
    std::string value(text);
    if (!value.empty() && value.front() == '%') {
        value.erase(value.begin());
    }
    return value;
}

%}

%defines "ir_parser.hpp"
%union {
    int ival;
    double fval;
    char* sval;
    
    ws::ir::Program* prog;
    ws::ir::GlobalDecl* gdecl;
    ws::ir::InteractionDecl* idecl;
    ws::ir::Stmt* stmt;
    ws::ir::Expr* expr;
    ws::ir::Type type_val;
    std::vector<ws::ir::GlobalDecl*>* gdecl_list;
    std::vector<ws::ir::InteractionDecl*>* idecl_list;
    std::vector<ws::ir::Stmt*>* stmt_list;
}

%token <sval> ID VAR_ID STRING TYPE_SYM
%token <fval> FLOAT_LIT
%token <ival> INT_LIT
%token T_GLOBAL T_INTERACTION T_FUNC T_STORE T_LOAD T_GLOBALLOAD
%token T_LAPLACIAN T_GRADIENT T_CAST T_CLAMP T_SELECT T_EXTRACT T_CONSTANT
%token T_COMPARE T_CONVERT
%token T_LPAREN T_RPAREN T_LBRACE T_RBRACE T_COMMA T_ASSIGN T_PERCENT
%token T_ADD T_SUB T_MUL T_DIV T_MOD T_POW T_MIN T_MAX T_AND T_OR T_GTE T_LT T_EQUAL
%token T_ABS T_SIN T_COS T_SQRT T_EXP T_LOG

%type <prog> program
%type <gdecl_list> globals
%type <idecl_list> interactions
%type <gdecl> global_decl
%type <idecl> interaction_decl
%type <stmt_list> stmt_list
%type <stmt> stmt
%type <expr> expr
%type <type_val> type
%type <ival> signed_int

%%

program:
    globals interactions {
        g_program = new Program();
        if ($1) {
            for (auto* g : *$1) g_program->globals.emplace_back(g);
            delete $1;
        }
        if ($2) {
            for (auto* i : *$2) g_program->interactions.emplace_back(i);
            delete $2;
        }
        $$ = g_program;
    }
    ;

globals:
    /* empty */ { $$ = new std::vector<GlobalDecl*>(); }
    | globals global_decl {
        $$ = $1;
        $$->push_back($2);
    }
    ;

global_decl:
    T_GLOBAL type ID T_ASSIGN expr {
        auto* decl = new GlobalDecl();
        decl->type = $2;
        decl->identifier = $3;
        decl->literal_expr.reset($5);
        free($3);
        $$ = decl;
    }
    | T_GLOBAL type ID {
        auto* decl = new GlobalDecl();
        decl->type = $2;
        decl->identifier = $3;
        free($3);
        $$ = decl;
    }
    ;

interactions:
    /* empty */ { $$ = new std::vector<InteractionDecl*>(); }
    | interactions interaction_decl {
        $$ = $1;
        $$->push_back($2);
    }
    ;

interaction_decl:
    T_INTERACTION T_LPAREN ID T_RPAREN T_FUNC ID T_LPAREN T_RPAREN T_LBRACE stmt_list T_RBRACE {
        auto* idecl = new InteractionDecl();
        idecl->id = $3;
        for (auto* s : *$10) idecl->stmts.emplace_back(s);
        delete $10;
        free($3);
        free($6);
        $$ = idecl;
    }
    | T_INTERACTION ID T_LBRACE stmt_list T_RBRACE {
        auto* idecl = new InteractionDecl();
        idecl->id = $2;
        for (auto* s : *$4) idecl->stmts.emplace_back(s);
        delete $4;
        free($2);
        $$ = idecl;
    }
    ;

stmt_list:
    /* empty */ { $$ = new std::vector<Stmt*>(); }
    | stmt_list stmt {
        $$ = $1;
        $$->push_back($2);
    }
    ;

stmt:
    type VAR_ID T_ASSIGN expr {
        auto* s = new VarDeclStmt();
        s->type = $1;
        s->identifier = stripPercentPrefix($2);
        s->expr.reset($4);
        free($2);
        $$ = s;
    }
    | VAR_ID T_ASSIGN expr {
        auto* s = new VarDeclStmt();
        s->type = Type::Unknown;
        s->identifier = stripPercentPrefix($1);
        s->expr.reset($3);
        free($1);
        $$ = s;
    }
    | T_STORE T_LPAREN STRING T_COMMA expr T_RPAREN {
        auto* s = new AssignStmt();
        s->field = $3;
        s->offset = 0;
        s->expr.reset($5);
        free($3);
        $$ = s;
    }
    | T_STORE T_LPAREN STRING T_COMMA INT_LIT T_COMMA expr T_RPAREN {
        auto* s = new AssignStmt();
        s->field = $3;
        s->offset = $5;
        s->expr.reset($7);
        free($3);
        $$ = s;
    }
    ;

expr:
    VAR_ID {
        auto* e = new VarRefExpr();
        e->name = $1;
        free($1);
        $$ = e;
    }
    | FLOAT_LIT {
        auto* e = new LiteralExpr();
        e->type = Type::F32;
        e->value = (float)$1;
        $$ = e;
    }
    | INT_LIT {
        auto* e = new LiteralExpr();
        e->type = Type::I32;
        e->value = $1;
        $$ = e;
    }
    | T_CONSTANT T_LPAREN FLOAT_LIT T_COMMA type T_RPAREN {
        auto* e = new ConstantExpr();
        e->type = $5;
        e->value = $3;
        $$ = e;
    }
    | T_CONSTANT T_LPAREN INT_LIT T_COMMA type T_RPAREN {
        auto* e = new ConstantExpr();
        e->type = $5;
        e->value = $3;
        $$ = e;
    }
    | T_LOAD T_LPAREN STRING T_RPAREN {
        auto* e = new LoadExpr();
        e->field = $3;
        e->x_offset = 0;
        e->y_offset = 0;
        free($3);
        $$ = e;
    }
    | T_LOAD T_LPAREN STRING T_COMMA INT_LIT T_COMMA INT_LIT T_RPAREN {
        auto* e = new LoadExpr();
        e->field = $3;
        e->x_offset = $5;
        e->y_offset = $7;
        free($3);
        $$ = e;
    }
    | T_LOAD T_LPAREN STRING T_COMMA signed_int T_COMMA signed_int T_RPAREN {
        auto* e = new LoadExpr();
        e->field = $3;
        e->x_offset = $5;
        e->y_offset = $7;
        free($3);
        $$ = e;
    }
    | T_GLOBALLOAD T_LPAREN STRING T_RPAREN {
        auto* e = new GlobalLoadExpr();
        e->field_name = $3;
        free($3);
        $$ = e;
    }
    | T_ADD T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::Add;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_SUB T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::Sub;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_MUL T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::Mul;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_DIV T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::Div;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_MOD T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::Mod;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_POW T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::Pow;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_MAX T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::Max;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_MIN T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::Min;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_AND T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::And;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_OR T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::Or;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_EQUAL T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::Equal;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_GTE T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::GreaterEqual;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_LT T_LPAREN expr T_COMMA expr T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = BinaryOp::LessThan;
        e->left.reset($3);
        e->right.reset($5);
        $$ = e;
    }
    | T_SIN T_LPAREN expr T_RPAREN {
        auto* e = new UnaryExpr();
        e->op = UnaryOp::Sin;
        e->operand.reset($3);
        $$ = e;
    }
    | T_ABS T_LPAREN expr T_RPAREN {
        auto* e = new UnaryExpr();
        e->op = UnaryOp::Abs;
        e->operand.reset($3);
        $$ = e;
    }
    | T_COS T_LPAREN expr T_RPAREN {
        auto* e = new UnaryExpr();
        e->op = UnaryOp::Cos;
        e->operand.reset($3);
        $$ = e;
    }
    | T_SQRT T_LPAREN expr T_RPAREN {
        auto* e = new UnaryExpr();
        e->op = UnaryOp::Sqrt;
        e->operand.reset($3);
        $$ = e;
    }
    | T_EXP T_LPAREN expr T_RPAREN {
        auto* e = new UnaryExpr();
        e->op = UnaryOp::Exp;
        e->operand.reset($3);
        $$ = e;
    }
    | T_LOG T_LPAREN expr T_RPAREN {
        auto* e = new UnaryExpr();
        e->op = UnaryOp::Log;
        e->operand.reset($3);
        $$ = e;
    }
    | T_CAST T_LPAREN expr T_COMMA type T_RPAREN {
        auto* e = new CastExpr();
        e->expr.reset($3);
        e->target_type = $5;
        $$ = e;
    }
    | T_CONVERT T_LPAREN expr T_COMMA type T_RPAREN {
        auto* e = new CastExpr();
        e->expr.reset($3);
        e->target_type = $5;
        $$ = e;
    }
    | T_EXTRACT T_LPAREN expr T_COMMA INT_LIT T_RPAREN {
        auto* e = new ExtractExpr();
        e->expr.reset($3);
        e->index = $5;
        $$ = e;
    }
    | T_SELECT T_LPAREN expr T_COMMA expr T_COMMA expr T_RPAREN {
        auto* e = new SelectExpr();
        e->condition.reset($3);
        e->true_expr.reset($5);
        e->false_expr.reset($7);
        $$ = e;
    }
    | T_CLAMP T_LPAREN expr T_COMMA expr T_COMMA expr T_RPAREN {
        auto* e = new ClampExpr();
        e->expr.reset($3);
        e->min_expr.reset($5);
        e->max_expr.reset($7);
        $$ = e;
    }
    | T_LAPLACIAN T_LPAREN STRING T_RPAREN {
        auto* e = new LaplacianExpr();
        e->field_name = $3;
        free($3);
        $$ = e;
    }
    | T_GRADIENT T_LPAREN STRING T_RPAREN {
        auto* e = new GradientExpr();
        e->field_name = $3;
        e->direction = 0; // or default
        free($3);
        $$ = e;
    }
    | T_COMPARE T_LPAREN expr T_COMMA expr T_COMMA STRING T_RPAREN {
        auto* e = new BinaryExpr();
        e->op = parseCompareOperator($7);
        e->left.reset($3);
        e->right.reset($5);
        free($7);
        $$ = e;
    }
    ;

type:
    TYPE_SYM {
        $$ = stringToType($1);
        free($1);
    }
    | STRING {
        $$ = stringToType($1);
        free($1);
    }
    ;

signed_int:
    INT_LIT {
        $$ = $1;
    }
    ;

%%

void yyerror(const char* s) {
    std::cerr << "IR Parse Error [line " << yylineno << "]: " << s << std::endl;
}
