%{
#include "ws/core/ir_ast.hpp"
#include "ir_parser.hpp"
#include <string.h>
#include <stdlib.h>

void yyerror(const char* s);
%}

%option noyywrap
%option yylineno

DIGIT    [0-9]
ID       [a-zA-Z_][a-zA-Z0-9_]*
EXP      [eE][+-]?{DIGIT}+
UFLOAT   (({DIGIT}+"."{DIGIT}*)|({DIGIT}*"."{DIGIT}+))({EXP})?|{DIGIT}+{EXP}
SFLOAT   -?{UFLOAT}
SINT     -?{DIGIT}+

%%

[ \t\n\r]+              { /* Skip whitespace */ }
"//".*                  { /* Skip comments */ }

"@global"               { return T_GLOBAL; }
"@interaction"          { return T_INTERACTION; }
"func"                  { return T_FUNC; }
"interaction"           { return T_INTERACTION; }
"Store"                 { return T_STORE; }
"Load"                  { return T_LOAD; }
"GlobalLoad"            { return T_GLOBALLOAD; }
"Add"                   { return T_ADD; }
"Sub"                   { return T_SUB; }
"Mul"                   { return T_MUL; }
"Div"                   { return T_DIV; }
"Mod"                   { return T_MOD; }
"And"                   { return T_AND; }
"Or"                    { return T_OR; }
"Equal"                 { return T_EQUAL; }
"Gte"                   { return T_GTE; }
"Lt"                    { return T_LT; }
"Min"                   { return T_MIN; }
"Max"                   { return T_MAX; }
"Pow"                   { return T_POW; }
"Abs"                   { return T_ABS; }
"Sin"                   { return T_SIN; }
"Cos"                   { return T_COS; }
"Sqrt"                  { return T_SQRT; }
"Exp"                   { return T_EXP; }
"Log"                   { return T_LOG; }
"Compare"               { return T_COMPARE; }
"Convert"               { return T_CONVERT; }
"Constant"              { return T_CONSTANT; }
"Cast"                  { return T_CAST; }
"Extract"               { return T_EXTRACT; }
"Select"                { return T_SELECT; }
"Clamp"                 { return T_CLAMP; }
"Laplacian"             { return T_LAPLACIAN; }
"Gradient"              { return T_GRADIENT; }
"f32"|"f64"|"i32"|"u32"|"bool"|"vec2"|"vec3" { 
                            yylval.sval = strdup(yytext); 
                            return TYPE_SYM; 
                        }

"("                     { return T_LPAREN; }
")"                     { return T_RPAREN; }
"{"                     { return T_LBRACE; }
"}"                     { return T_RBRACE; }
","                     { return T_COMMA; }
"="                     { return T_ASSIGN; }

{SFLOAT}                { yylval.fval = atof(yytext); return FLOAT_LIT; }
{SINT}                  { yylval.ival = atoi(yytext); return INT_LIT; }

%[a-zA-Z_0-9]+          { yylval.sval = strdup(yytext); return VAR_ID; }
\"([^\"\\]|\\.)*\"      { 
                            yylval.sval = strdup(yytext+1); 
                            yylval.sval[strlen(yylval.sval)-1] = '\0'; 
                            return STRING; 
                        }
{ID}                    { yylval.sval = strdup(yytext); return ID; }

.                       { yyerror("Unknown character"); }

%%
