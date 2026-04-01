#include "ws/core/ir_ast.hpp"
#include <iostream>
#include <stdexcept>

struct yy_buffer_state;
typedef yy_buffer_state *YY_BUFFER_STATE;

YY_BUFFER_STATE yy_scan_string(const char * str);
void yy_delete_buffer(YY_BUFFER_STATE buffer);

extern int yyparse();
extern ws::ir::Program* g_program;

ws::ir::Program* g_program = nullptr;

namespace ws::ir {

Program parse_ir(const std::string& source) {
    g_program = nullptr;
    
    YY_BUFFER_STATE state = yy_scan_string(source.c_str());
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
