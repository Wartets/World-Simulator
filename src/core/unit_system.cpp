#include "ws/core/unit_system.hpp"
#include <sstream>
#include <cctype>
#include <vector>

namespace ws {

UnitMismatchError::UnitMismatchError(const SIUnit& a, const SIUnit& b, const std::string& context)
    : std::runtime_error("Unit mismatch in " + context + ": dimension <" + a.toString() + "> is incompatible with <" + b.toString() + ">") {}

bool SIUnit::isDimensionless() const noexcept {
    return kg == 0 && m == 0 && s == 0 && K == 0 && A == 0 && mol == 0 && cd == 0;
}

bool SIUnit::operator==(const SIUnit& other) const noexcept {
    return kg == other.kg && m == other.m && s == other.s &&
           K == other.K && A == other.A && mol == other.mol && cd == other.cd;
}

bool SIUnit::operator!=(const SIUnit& other) const noexcept {
    return !(*this == other);
}

SIUnit SIUnit::operator*(const SIUnit& other) const noexcept {
    SIUnit result;
    result.kg = kg + other.kg;
    result.m = m + other.m;
    result.s = s + other.s;
    result.K = K + other.K;
    result.A = A + other.A;
    result.mol = mol + other.mol;
    result.cd = cd + other.cd;
    return result;
}

SIUnit SIUnit::operator/(const SIUnit& other) const noexcept {
    SIUnit result;
    result.kg = kg - other.kg;
    result.m = m - other.m;
    result.s = s - other.s;
    result.K = K - other.K;
    result.A = A - other.A;
    result.mol = mol - other.mol;
    result.cd = cd - other.cd;
    return result;
}

SIUnit SIUnit::add(const SIUnit& other) const {
    if (*this != other) {
        throw UnitMismatchError(*this, other, "addition/subtraction");
    }
    return *this;
}

SIUnit SIUnit::sub(const SIUnit& other) const {
    return add(other);
}

SIUnit SIUnit::dimensionless() noexcept {
    return SIUnit{};
}

enum TokenType { TOK_ID, TOK_NUM, TOK_MUL, TOK_DIV, TOK_POW, TOK_LPAREN, TOK_RPAREN, TOK_EOF };
struct Token {
    TokenType type;
    std::string val;
};

static std::vector<Token> tokenize(const std::string& input) {
    std::vector<Token> output;
    size_t i = 0;
    while (i < input.length()) {
        char c = input[i];
        if (std::isspace(c)) { i++; continue; }
        if (c == '*') { output.push_back({TOK_MUL, "*"}); i++; }
        else if (c == '/') { output.push_back({TOK_DIV, "/"}); i++; }
        else if (c == '^') { output.push_back({TOK_POW, "^"}); i++; }
        else if (c == '(') { output.push_back({TOK_LPAREN, "("}); i++; }
        else if (c == ')') { output.push_back({TOK_RPAREN, ")"}); i++; }
        else if (std::isalpha(c)) {
            std::string id;
            while (i < input.length() && std::isalpha(input[i])) {
                id += input[i++];
            }
            output.push_back({TOK_ID, id});
        }
        else if (std::isdigit(c) || c == '-') {
            std::string num;
            if (c == '-') { num += c; i++; }
            while (i < input.length() && std::isdigit(input[i])) {
                num += input[i++];
            }
            output.push_back({TOK_NUM, num});
        } else {
            throw std::runtime_error("Invalid character in unit string: " + std::string(1, c));
        }
    }
    output.push_back({TOK_EOF, ""});
    return output;
}

static SIUnit parseFactor(const std::vector<Token>& tokens, size_t& pos);

static SIUnit parsePrimary(const std::vector<Token>& tokens, size_t& pos) {
    if (tokens[pos].type == TOK_ID) {
        SIUnit u;
        std::string id = tokens[pos].val;
        pos++;
        if (id == "kg") u.kg = 1;
        else if (id == "m") u.m = 1;
        else if (id == "s") u.s = 1;
        else if (id == "K") u.K = 1;
        else if (id == "A") u.A = 1;
        else if (id == "mol") u.mol = 1;
        else if (id == "cd") u.cd = 1;
        else if (id == "1") ; // dimensionless
        else throw std::runtime_error("Unknown SI base unit: " + id);
        
        if (tokens[pos].type == TOK_POW) {
            pos++;
            if (tokens[pos].type != TOK_NUM) throw std::runtime_error("Expected exponent after ^");
            int exp = std::stoi(tokens[pos].val);
            pos++;
            u.kg *= exp; u.m *= exp; u.s *= exp; u.K *= exp; u.A *= exp; u.mol *= exp; u.cd *= exp;
        }
        return u;
    } else if (tokens[pos].type == TOK_NUM && tokens[pos].val == "1") {
        pos++; // Dimensionless "1"
        return SIUnit{};
    } else if (tokens[pos].type == TOK_LPAREN) {
        pos++;
        SIUnit result = parseFactor(tokens, pos);
        if (tokens[pos].type != TOK_RPAREN) throw std::runtime_error("Expected closing parenthesis ')'");
        pos++;
        if (tokens[pos].type == TOK_POW) {
            pos++;
            if (tokens[pos].type != TOK_NUM) throw std::runtime_error("Expected exponent after ^");
            int exp = std::stoi(tokens[pos].val);
            pos++;
            result.kg *= exp; result.m *= exp; result.s *= exp; result.K *= exp; result.A *= exp; result.mol *= exp; result.cd *= exp;
        }
        return result;
    }
    throw std::runtime_error("Unexpected token in unit expression: " + tokens[pos].val);
}

static SIUnit parseFactor(const std::vector<Token>& tokens, size_t& pos) {
    SIUnit result = parsePrimary(tokens, pos);
    while (tokens[pos].type == TOK_MUL || tokens[pos].type == TOK_DIV) {
        TokenType op = tokens[pos].type;
        pos++;
        SIUnit right = parsePrimary(tokens, pos);
        if (op == TOK_MUL) result = result * right;
        else result = result / right;
    }
    return result;
}

SIUnit SIUnit::parse(const std::string& unitStr) {
    if (unitStr.empty() || unitStr == "1" || unitStr == "none") return dimensionless();
    auto tokens = tokenize(unitStr);
    size_t pos = 0;
    SIUnit result = parseFactor(tokens, pos);
    if (tokens[pos].type != TOK_EOF) {
        throw std::runtime_error("Extra tokens at the end of unit expression");
    }
    return result;
}

std::string SIUnit::toString() const {
    if (isDimensionless()) return "1";
    std::ostringstream num, den;
    
    auto addTerm = [](std::ostringstream& ss, const char* name, int8_t exp, bool isFirst) {
        if (!isFirst) ss << "*";
        ss << name;
        if (exp > 1) ss << "^" << (int)exp;
    };
    
    bool nFirst = true, dFirst = true;
    
    if (kg > 0) { addTerm(num, "kg", kg, nFirst); nFirst = false; }
    if (kg < 0) { addTerm(den, "kg", -kg, dFirst); dFirst = false; }
    
    if (m > 0) { addTerm(num, "m", m, nFirst); nFirst = false; }
    if (m < 0) { addTerm(den, "m", -m, dFirst); dFirst = false; }
    
    if (s > 0) { addTerm(num, "s", s, nFirst); nFirst = false; }
    if (s < 0) { addTerm(den, "s", -s, dFirst); dFirst = false; }
    
    if (K > 0) { addTerm(num, "K", K, nFirst); nFirst = false; }
    if (K < 0) { addTerm(den, "K", -K, dFirst); dFirst = false; }
    
    if (A > 0) { addTerm(num, "A", A, nFirst); nFirst = false; }
    if (A < 0) { addTerm(den, "A", -A, dFirst); dFirst = false; }
    
    if (mol > 0) { addTerm(num, "mol", mol, nFirst); nFirst = false; }
    if (mol < 0) { addTerm(den, "mol", -mol, dFirst); dFirst = false; }
    
    if (cd > 0) { addTerm(num, "cd", cd, nFirst); nFirst = false; }
    if (cd < 0) { addTerm(den, "cd", -cd, dFirst); dFirst = false; }

    std::string numStr = nFirst ? "1" : num.str();
    if (dFirst) return numStr;
    if (!nFirst && num.str().find('*') != std::string::npos && !dFirst) {
        // purely cosmetic, but fine enough
    }
    
    bool dMulti = den.str().find('*') != std::string::npos;
    if (dMulti) return numStr + "/(" + den.str() + ")";
    else return numStr + "/" + den.str();
}

} // namespace ws
