#include "ws/gui/shader_editor.hpp"

#include <cassert>
#include <iostream>
#include <sstream>

namespace ws::gui {

// =============================================================================
// Test Assertion Macros
// =============================================================================

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "ASSERTION FAILED at " << __FILE__ << ":" << __LINE__ << ": " << #cond << std::endl; \
            return false; \
        } \
    } while (false)

#define TEST_EQUAL(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << "EQUALITY CHECK FAILED at " << __FILE__ << ":" << __LINE__ << ": " << #a << " != " << #b << std::endl; \
            return false; \
        } \
    } while (false)

#define TEST_NOT_EQUAL(a, b) \
    do { \
        if ((a) == (b)) { \
            std::cerr << "NOT-EQUAL CHECK FAILED at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (false)

#define TEST_TRUE(cond) TEST_ASSERT(cond)
#define TEST_FALSE(cond) TEST_ASSERT(!(cond))

#define TEST_STRING_CONTAINS(haystack, needle) \
    do { \
        if ((haystack).find((needle)) == std::string::npos) { \
            std::cerr << "STRING CONTAINS CHECK FAILED at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::cerr << "  Expected to find: " << (needle) << std::endl; \
            std::cerr << "  In: " << (haystack) << std::endl; \
            return false; \
        } \
    } while (false)

// =============================================================================
// Test Cases
// =============================================================================

bool testValidFragmentShader() {
    std::string validShader = R"(
        uniform vec3 color;
        
        void main() {
            gl_FragColor = vec4(color, 1.0);
        }
    )";

    ShaderCompilationResult result = ShaderValidator::validate(validShader, "fragment");
    TEST_TRUE(result.success);
    TEST_EQUAL(result.errors.size(), 0ul);
    return true;
}

bool testValidVertexShader() {
    std::string validShader = R"(
        attribute vec3 position;
        
        void main() {
            gl_Position = vec4(position, 1.0);
        }
    )";

    ShaderCompilationResult result = ShaderValidator::validate(validShader, "vertex");
    TEST_TRUE(result.success);
    return true;
}

bool testEmptyShaderFails() {
    std::string emptyShader = "";
    ShaderCompilationResult result = ShaderValidator::validate(emptyShader, "fragment");
    TEST_FALSE(result.success);
    TEST_TRUE(result.errors.size() > 0);
    TEST_EQUAL(result.errors[0].code, "empty_source");
    return true;
}

bool testMissingMainFails() {
    std::string noMain = R"(
        uniform vec3 color;
        float someFunction() {
            return 1.0;
        }
    )";

    ShaderCompilationResult result = ShaderValidator::validate(noMain, "fragment");
    TEST_FALSE(result.success);
    TEST_TRUE(result.errors.size() > 0);
    TEST_EQUAL(result.errors[0].code, "missing_main");
    return true;
}

bool testUnbalancedBracesFails() {
    std::string unbalanced = R"(
        void main() {
            gl_FragColor = vec4(0.0);
    )";

    ShaderCompilationResult result = ShaderValidator::validate(unbalanced, "fragment");
    TEST_FALSE(result.success);
    TEST_EQUAL(result.errors[0].code, "syntax_error");
    return true;
}

bool testDetectUniforms() {
    std::string shader = R"(
        uniform vec3 color;
        uniform float alpha;
        uniform mat4 transform;
        
        void main() {
            gl_FragColor = vec4(color, alpha);
        }
    )";

    auto bindings = ShaderValidator::detectResourceBindings(shader);
    TEST_EQUAL(bindings.size(), 3ul);
    
    TEST_TRUE(bindings[0].isUniform);
    TEST_EQUAL(bindings[0].name, "color");
    TEST_EQUAL(bindings[0].type, "vec3");
    
    TEST_TRUE(bindings[1].isUniform);
    TEST_EQUAL(bindings[1].name, "alpha");
    TEST_EQUAL(bindings[1].type, "float");
    
    TEST_TRUE(bindings[2].isUniform);
    TEST_EQUAL(bindings[2].name, "transform");
    TEST_EQUAL(bindings[2].type, "mat4");
    
    return true;
}

bool testDetectAttributes() {
    std::string shader = R"(
        attribute vec3 position;
        attribute vec2 texCoord;
        
        void main() {
            gl_Position = vec4(position, 1.0);
        }
    )";

    auto bindings = ShaderValidator::detectResourceBindings(shader);
    TEST_EQUAL(bindings.size(), 2ul);
    TEST_FALSE(bindings[0].isUniform);
    TEST_FALSE(bindings[1].isUniform);
    return true;
}

bool testSecurityConstraintsFileIO() {
    std::string shaderWithFileIO = R"(
        void main() {
            imageStore(uImage, ivec2(0), vec4(0.0));
        }
    )";

    ShaderSandboxConfig config;
    config.allowFileI_O = true;  // Disallow file I/O
    
    std::string issue = ShaderValidator::checkSecurityConstraints(shaderWithFileIO, config);
    TEST_FALSE(issue.empty());
    TEST_STRING_CONTAINS(issue, "File I/O");
    return true;
}

bool testComplexityEstimate() {
    std::string simpleShader = R"(
        void main() {
            gl_FragColor = vec4(1.0);
        }
    )";

    int complexity1 = ShaderValidator::estimateComplexity(simpleShader);
    TEST_TRUE(complexity1 > 0);

    std::string complexShader = R"(
        void main() {
            for (int i = 0; i < 100; ++i) {
                for (int j = 0; j < 100; ++j) {
                    gl_FragColor = vec4(float(i) / 100.0);
                }
            }
        }
    )";

    int complexity2 = ShaderValidator::estimateComplexity(complexShader);
    TEST_TRUE(complexity2 > complexity1);
    return true;
}

bool testUniformLimitExceeded() {
    std::string manyUniforms = R"(
        uniform float u0;
        uniform float u1;
        uniform float u2;
        uniform float u3;
        uniform float u4;
        uniform float u5;
        uniform float u6;
        uniform float u7;
        uniform float u8;
        uniform float u9;
        uniform float u10;
        uniform float u11;
        uniform float u12;
        uniform float u13;
        uniform float u14;
        uniform float u15;
        uniform float u16;
        uniform float u17;
        uniform float u18;
        uniform float u19;
        uniform float u20;
        uniform float u21;
        uniform float u22;
        uniform float u23;
        uniform float u24;
        uniform float u25;
        uniform float u26;
        uniform float u27;
        uniform float u28;
        uniform float u29;
        uniform float u30;
        uniform float u31;
        uniform float u32;
        uniform float u33;
        
        void main() {
            gl_FragColor = vec4(0.0);
        }
    )";

    ShaderSandboxConfig config;
    config.maxUniformCount = 32;  // Limit to 32

    ShaderCompilationResult result = ShaderValidator::validate(manyUniforms, "fragment", config);
    TEST_FALSE(result.success);
    TEST_TRUE(result.errors.size() > 0);
    TEST_EQUAL(result.errors[0].code, "too_many_uniforms");
    return true;
}

bool testShaderEditorCompile() {
    ShaderEditor editor;
    
    std::string validShader = R"(
        uniform vec3 color;
        
        void main() {
            gl_FragColor = vec4(color, 1.0);
        }
    )";

    editor.setSourceCode(validShader);
    auto result = editor.compile("fragment");
    
    TEST_TRUE(result.success);
    return true;
}

bool testShaderEditorAcceptAndRevert() {
    ShaderEditor editor;
    
    std::string shader1 = R"(
        void main() {
            gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
        }
    )";

    editor.setSourceCode(shader1);
    editor.compile("fragment");
    editor.acceptCompiledShader();
    
    // Change shader to invalid
    editor.setSourceCode("invalid");
    
    // Revert to last valid
    editor.revertToLastValid();
    TEST_EQUAL(editor.getSourceCode(), shader1);
    
    return true;
}

bool testShaderEditorLivePreview() {
    ShaderEditor editor;
    TEST_FALSE(editor.isLivePreviewEnabled());
    
    editor.setLivePreviewEnabled(true);
    TEST_TRUE(editor.isLivePreviewEnabled());
    
    editor.setLivePreviewEnabled(false);
    TEST_FALSE(editor.isLivePreviewEnabled());
    
    return true;
}

bool testCodeFormatting() {
    std::string unformatted = "void main(){gl_FragColor=vec4(1.0);}";
    std::string formatted = ShaderValidator::formatCode(unformatted);
    
    // Formatted version should have some structure
    TEST_TRUE(formatted.length() > unformatted.length());
    TEST_STRING_CONTAINS(formatted, "\n");
    return true;
}

bool testDetectedBindingsNotEmpty() {
    std::string shader = R"(
        uniform sampler2D texture;
        attribute vec2 uv;
        varying vec2 vUV;
        
        void main() {
            vUV = uv;
        }
    )";

    ShaderEditor editor;
    editor.setSourceCode(shader);
    editor.compile("vertex");
    
    auto bindings = editor.getDetectedBindings();
    TEST_TRUE(bindings.size() > 0);
    return true;
}

} // namespace ws::gui

// =============================================================================
// Test Runner
// =============================================================================

int main() {
    std::cout << "Running shader editor tests...\n" << std::endl;

    struct TestCase {
        const char* name;
        bool (*test)();
    };

    const TestCase tests[] = {
        {"Valid Fragment Shader", ws::gui::testValidFragmentShader},
        {"Valid Vertex Shader", ws::gui::testValidVertexShader},
        {"Empty Shader Fails", ws::gui::testEmptyShaderFails},
        {"Missing Main Fails", ws::gui::testMissingMainFails},
        {"Unbalanced Braces Fails", ws::gui::testUnbalancedBracesFails},
        {"Detect Uniforms", ws::gui::testDetectUniforms},
        {"Detect Attributes", ws::gui::testDetectAttributes},
        {"Security Constraints FileIO", ws::gui::testSecurityConstraintsFileIO},
        {"Complexity Estimate", ws::gui::testComplexityEstimate},
        {"Uniform Limit Exceeded", ws::gui::testUniformLimitExceeded},
        {"Shader Editor Compile", ws::gui::testShaderEditorCompile},
        {"Shader Editor Accept and Revert", ws::gui::testShaderEditorAcceptAndRevert},
        {"Shader Editor Live Preview", ws::gui::testShaderEditorLivePreview},
        {"Code Formatting", ws::gui::testCodeFormatting},
        {"Detected Bindings Not Empty", ws::gui::testDetectedBindingsNotEmpty},
    };

    int passCount = 0;
    int failCount = 0;

    for (const auto& tc : tests) {
        std::cout << "  " << tc.name << "... ";
        try {
            if (tc.test()) {
                std::cout << "PASS" << std::endl;
                ++passCount;
            } else {
                std::cout << "FAIL" << std::endl;
                ++failCount;
            }
        } catch (const std::exception& e) {
            std::cout << "EXCEPTION: " << e.what() << std::endl;
            ++failCount;
        } catch (...) {
            std::cout << "UNKNOWN EXCEPTION" << std::endl;
            ++failCount;
        }
    }

    std::cout << "\n" << passCount << "/" << (passCount + failCount) << " tests passed" << std::endl;

    return (failCount == 0) ? 0 : 1;
}
