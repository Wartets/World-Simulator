#include "ws/core/ir_ast.hpp"
#include <iostream>
#include <stdexcept>

struct yy_buffer_state;
typedef yy_buffer_state *YY_BUFFER_STATE;

YY_BUFFER_STATE yy_scan_string(const char * str);
void yy_delete_buffer(YY_BUFFER_STATE buffer);

extern int yyparse();
extern int yylineno;
extern ws::ir::Program* g_program;

ws::ir::Program* g_program = nullptr;

namespace ws::ir {

// Parses IR source code and returns the resulting AST program.
// Throws std::runtime_error if parsing fails.
Program parse_ir(const std::string& source) {
    g_program = nullptr;
    yylineno = 1;

    std::string normalized = source;
    if (normalized.size() >= 3 &&
        static_cast<unsigned char>(normalized[0]) == 0xEF &&
        static_cast<unsigned char>(normalized[1]) == 0xBB &&
        static_cast<unsigned char>(normalized[2]) == 0xBF) {
        normalized.erase(0, 3);
    }

    YY_BUFFER_STATE state = yy_scan_string(normalized.c_str());
    int result = yyparse();
    yy_delete_buffer(state);
    
    if (result == 0 && g_program != nullptr) {
        Program p = std::move(*g_program);
        delete g_program;
        g_program = nullptr;
        return p;
    } else {
        if (g_program) {
            delete g_program;
            g_program = nullptr;
        }
        throw std::runtime_error("Failed to parse IR source code.");
    }
}

} // namespace ws::ir
