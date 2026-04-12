#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ws::gui {

// =============================================================================
// Shader Compilation Error
// =============================================================================

// Represents a compilation error from shader validation.
struct ShaderCompilationError {
    int lineNumber = 0;           // Line where error occurred (1-based).
    std::string message;          // Error message text.
    std::string code;             // Error code (e.g., "syntax_error", "undefined_uniform").
};

// =============================================================================
// Shader Compilation Result
// =============================================================================

// Result of shader validation and compilation attempt.
struct ShaderCompilationResult {
    bool success = false;                           // Whether compilation succeeded.
    std::vector<ShaderCompilationError> errors;     // List of compilation errors.
    std::string warningLog;                         // Non-fatal warnings (if any).
    std::string compiledBinary;                     // Compiled shader binary (if successful).
};

// =============================================================================
// Shader Resource Binding
// =============================================================================

// Metadata about a uniform or attribute in the shader.
struct ShaderResourceBinding {
    std::string name;           // Uniform/attribute name.
    std::string type;           // Type (e.g., "float", "vec3", "sampler2D").
    int location = -1;          // Binding location (if applicable).
    bool isUniform = false;     // Whether this is a uniform (vs attribute/varying).
};

// =============================================================================
// Shader Sandbox Configuration
// =============================================================================

// Security and safety constraints for shader execution.
struct ShaderSandboxConfig {
    bool allowFileI_O = false;                      // Prevent file read/write operations.
    bool allowExternalCalls = false;                // Prevent calls to external functions.
    int maxShaderComplexity = 10000;                // Max instruction count for complexity check.
    int maxUniformCount = 32;                       // Max allowed uniform count.
    bool validateInputOutputBindings = true;        // Ensure all I/O bindings are valid.
};

// =============================================================================
// Shader Editor State
// =============================================================================

// Tracks the active shader being edited and compiled.
struct ShaderEditorState {
    std::string sourceCode;                         // Current GLSL source code.
    std::string previousValidSource;                // Last known good shader source.
    ShaderCompilationResult lastResult;             // Result of last compilation.
    std::vector<ShaderResourceBinding> detectedBindings;  // Uniforms/attributes found.
    bool livePreviewEnabled = false;                // Whether to apply shader on-the-fly.
    bool autoCompileOnChange = true;                // Auto-compile when source changes.
};

// =============================================================================
// Shader Validator and Compiler
// =============================================================================

class ShaderValidator {
public:
    // Validates and compiles GLSL shader source code.
    // @param source GLSL source code to validate
    // @param stage Shader stage (e.g., "vertex", "fragment")
    // @param config Security and validation configuration
    // @return Compilation result with errors or success
    static ShaderCompilationResult validate(
        const std::string& source,
        const std::string& stage,
        const ShaderSandboxConfig& config = ShaderSandboxConfig());

    // Detects uniform and attribute declarations in shader source.
    // @param source GLSL source code
    // @return Vector of detected resource bindings
    static std::vector<ShaderResourceBinding> detectResourceBindings(const std::string& source);

    // Checks for common security issues (file I/O, external calls, etc.).
    // @param source GLSL source code
    // @param config Security configuration to enforce
    // @return Error message if issue found, empty string if clean
    static std::string checkSecurityConstraints(const std::string& source, const ShaderSandboxConfig& config);

    // Estimates shader complexity (instruction count, branch depth, etc.).
    // @param source GLSL source code
    // @return Complexity score
    static int estimateComplexity(const std::string& source);

    // Formats GLSL shader source code.
    // @param source Input GLSL code
    // @return Formatted source with proper indentation
    static std::string formatCode(const std::string& source);
};

// =============================================================================
// Shader Editor
// =============================================================================

class ShaderEditor {
public:
    ShaderEditor() = default;
    ~ShaderEditor() = default;

    // Sets the source code being edited.
    void setSourceCode(const std::string& source) { state_.sourceCode = source; }

    // Gets the current source code.
    [[nodiscard]] const std::string& getSourceCode() const { return state_.sourceCode; }

    // Compiles the current shader source.
    // @param stage Shader stage (vertex, fragment, etc.)
    // @param config Sandbox configuration
    // @return Compilation result
    ShaderCompilationResult compile(const std::string& stage, const ShaderSandboxConfig& config = ShaderSandboxConfig());

    // Accepts the current compiled shader, saving it as the last valid version.
    void acceptCompiledShader();

    // Reverts to the last known-good shader source.
    void revertToLastValid();

    // Enables or disables live preview mode.
    void setLivePreviewEnabled(bool enabled) { state_.livePreviewEnabled = enabled; }

    // Checks if live preview is enabled.
    [[nodiscard]] bool isLivePreviewEnabled() const { return state_.livePreviewEnabled; }

    // Gets the compilation result from the last compile operation.
    [[nodiscard]] const ShaderCompilationResult& getLastCompilationResult() const { return state_.lastResult; }

    // Gets detected resource bindings (uniforms, attributes, etc.).
    [[nodiscard]] const std::vector<ShaderResourceBinding>& getDetectedBindings() const {
        return state_.detectedBindings;
    }

    // Gets the current editor state.
    [[nodiscard]] const ShaderEditorState& getState() const { return state_; }

private:
    ShaderEditorState state_;
};

} // namespace ws::gui

