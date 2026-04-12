#include "ws/gui/shader_editor.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace ws::gui {

// Helper: Check if a string is a reserved GLSL keyword
static bool isGlslKeyword(const std::string& word) {
    static const std::vector<std::string> keywords = {
        "attribute", "const", "uniform", "varying", "break", "continue", "do", "for", "while",
        "switch", "case", "default", "if", "else", "in", "out", "inout", "centroid", "flat",
        "smooth", "layout", "smooth", "patch", "sample", "buffer", "shared", "coherent",
        "volatile", "restrict", "readonly", "writeonly", "struct", "void", "bool", "int",
        "uint", "float", "double", "bvec2", "bvec3", "bvec4", "ivec2", "ivec3", "ivec4",
        "uvec2", "uvec3", "uvec4", "vec2", "vec3", "vec4", "dvec2", "dvec3", "dvec4",
        "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4", "mat3x2", "mat3x3", "mat3x4",
        "mat4x2", "mat4x3", "mat4x4", "dmat2", "dmat3", "dmat4", "sampler1D", "isampler1D",
        "usampler1D", "sampler2D", "isampler2D", "usampler2D", "sampler3D", "isampler3D",
        "usampler3D", "discard", "return"
    };
    return std::find(keywords.begin(), keywords.end(), word) != keywords.end();
}

// Helper: Tokenize GLSL source code
static std::vector<std::string> tokenizeGlsl(const std::string& source) {
    std::vector<std::string> tokens;
    std::istringstream stream(source);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// Helper: Count balanced braces
static bool hasBalancedBraces(const std::string& source) {
    int depth = 0;
    for (char c : source) {
        if (c == '{') ++depth;
        else if (c == '}') --depth;
        if (depth < 0) return false;
    }
    return depth == 0;
}

// Helper: Count lines in source
static int countLines(const std::string& source) {
    return static_cast<int>(std::count(source.begin(), source.end(), '\n')) + 1;
}

// ============================================================================
// ShaderValidator Implementation
// ============================================================================

ShaderCompilationResult ShaderValidator::validate(
    const std::string& source,
    const std::string& stage,
    const ShaderSandboxConfig& config) {
    
    ShaderCompilationResult result;
    result.success = false;
    result.errors.clear();

    // Check for empty source
    if (source.empty()) {
        result.errors.push_back({1, "Shader source is empty", "empty_source"});
        return result;
    }

    // Check brace balance
    if (!hasBalancedBraces(source)) {
        result.errors.push_back({1, "Unbalanced braces in shader", "syntax_error"});
        return result;
    }

    // Check security constraints
    std::string securityIssue = checkSecurityConstraints(source, config);
    if (!securityIssue.empty()) {
        result.errors.push_back({1, securityIssue, "security_violation"});
        return result;
    }

    // Check complexity
    int complexity = estimateComplexity(source);
    if (complexity > config.maxShaderComplexity) {
        result.errors.push_back({
            1,
            "Shader exceeds complexity limit (score: " + std::to_string(complexity) + ")",
            "complexity_exceeded"
        });
        return result;
    }

    // Detect resource bindings
    auto bindings = detectResourceBindings(source);
    if (static_cast<int>(bindings.size()) > config.maxUniformCount) {
        result.errors.push_back({
            1,
            "Too many uniforms (limit: " + std::to_string(config.maxUniformCount) + ")",
            "too_many_uniforms"
        });
        return result;
    }

    // Basic syntax validation: check for main() function
    if (source.find("main()") == std::string::npos && source.find("main ()") == std::string::npos) {
        result.errors.push_back({1, "Shader must define main() function", "missing_main"});
        return result;
    }

    // If all checks pass, shader compiles successfully
    result.success = true;
    result.compiledBinary = source;  // In a real implementation, this would be compiled to SPIR-V
    return result;
}

std::vector<ShaderResourceBinding> ShaderValidator::detectResourceBindings(const std::string& source) {
    std::vector<ShaderResourceBinding> bindings;
    std::istringstream stream(source);
    std::string line;
    int lineNum = 0;

    // Regex patterns for uniform/attribute declarations
    std::regex uniformPattern(R"(^\s*uniform\s+(\w+)\s+(\w+))");
    std::regex attributePattern(R"(^\s*attribute\s+(\w+)\s+(\w+))");
    std::regex varyingPattern(R"(^\s*varying\s+(\w+)\s+(\w+))");

    while (std::getline(stream, line)) {
        ++lineNum;
        
        std::smatch match;
        
        // Check for uniform declaration
        if (std::regex_search(line, match, uniformPattern)) {
            ShaderResourceBinding binding;
            binding.type = match[1].str();
            binding.name = match[2].str();
            binding.isUniform = true;
            bindings.push_back(binding);
        }
        
        // Check for attribute declaration
        if (std::regex_search(line, match, attributePattern)) {
            ShaderResourceBinding binding;
            binding.type = match[1].str();
            binding.name = match[2].str();
            binding.isUniform = false;
            bindings.push_back(binding);
        }
        
        // Check for varying declaration
        if (std::regex_search(line, match, varyingPattern)) {
            ShaderResourceBinding binding;
            binding.type = match[1].str();
            binding.name = match[2].str();
            binding.isUniform = false;
            bindings.push_back(binding);
        }
    }

    return bindings;
}

std::string ShaderValidator::checkSecurityConstraints(const std::string& source, const ShaderSandboxConfig& config) {
    // Check for forbidden operations
    if (config.allowFileI_O) {
        if (source.find("imageLoad") != std::string::npos ||
            source.find("imageStore") != std::string::npos ||
            source.find("textureLoad") != std::string::npos) {
            return "File I/O operations are not allowed in sandboxed shaders";
        }
    }

    // Check for forbidden external calls
    if (config.allowExternalCalls) {
        // Check for calls to non-standard functions
        std::vector<std::string> forbiddenFunctions = {
            "barrier", "memoryBarrier", "groupMemoryBarrier"
        };
        for (const auto& func : forbiddenFunctions) {
            if (source.find(func + "(") != std::string::npos) {
                return "Function '" + func + "' is not allowed in sandboxed shaders";
            }
        }
    }

    return "";  // No security issues
}

int ShaderValidator::estimateComplexity(const std::string& source) {
    int score = 0;
    
    // Count statements
    score += static_cast<int>(std::count(source.begin(), source.end(), ';'));
    
    // Count loops (add extra weight)
    score += static_cast<int>(std::count(source.begin(), source.end(), '{')) * 2;
    
    // Count function calls
    score += static_cast<int>(std::count(source.begin(), source.end(), '('));
    
    // Count lines
    score += countLines(source);
    
    return score;
}

std::string ShaderValidator::formatCode(const std::string& source) {
    std::string formatted;
    int indentLevel = 0;
    bool inString = false;
    bool inComment = false;

    for (std::size_t i = 0; i < source.length(); ++i) {
        char c = source[i];
        char nextC = (i + 1 < source.length()) ? source[i + 1] : '\0';

        // Handle string literals
        if (c == '"' && (i == 0 || source[i - 1] != '\\')) {
            inString = !inString;
            formatted += c;
            continue;
        }

        // Handle comments
        if (!inString && c == '/' && nextC == '/') {
            while (i < source.length() && source[i] != '\n') {
                formatted += source[i++];
            }
            formatted += '\n';
            continue;
        }

        if (!inString) {
            if (c == '{') {
                formatted += " {\n";
                ++indentLevel;
                for (int j = 0; j < indentLevel; ++j) formatted += "  ";
            } else if (c == '}') {
                --indentLevel;
                if (!formatted.empty() && formatted.back() != '\n') {
                    formatted += "\n";
                }
                for (int j = 0; j < indentLevel; ++j) formatted += "  ";
                formatted += "}\n";
            } else if (c == ';') {
                formatted += ";\n";
                for (int j = 0; j < indentLevel; ++j) formatted += "  ";
            } else if (c == '\n') {
                formatted += '\n';
                for (int j = 0; j < indentLevel; ++j) formatted += "  ";
            } else if (!std::isspace(static_cast<unsigned char>(c))) {
                formatted += c;
            }
        } else {
            formatted += c;
        }
    }

    return formatted;
}

// ============================================================================
// ShaderEditor Implementation
// ============================================================================

ShaderCompilationResult ShaderEditor::compile(const std::string& stage, const ShaderSandboxConfig& config) {
    state_.lastResult = ShaderValidator::validate(state_.sourceCode, stage, config);
    state_.detectedBindings = ShaderValidator::detectResourceBindings(state_.sourceCode);
    return state_.lastResult;
}

void ShaderEditor::acceptCompiledShader() {
    if (state_.lastResult.success) {
        state_.previousValidSource = state_.sourceCode;
    }
}

void ShaderEditor::revertToLastValid() {
    if (!state_.previousValidSource.empty()) {
        state_.sourceCode = state_.previousValidSource;
    }
}

} // namespace ws::gui

