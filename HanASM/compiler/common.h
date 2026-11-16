#ifndef ZHASM_COMMON_H
#define ZHASM_COMMON_H

// ============================================================================
//  中文汇编器（Chinese Assembler）—— 公共定义
// ----------------------------------------------------------------------------
//  目标：把中文 / 英文混合书写的汇编源码，翻译成 DOS COM 格式（加载地址
//        固定为 0x100）的平坦二进制机器码文件。
//
//  本文件只放"全局共享"的东西：
//    1. 词法记号类型 TokenType 与 Token 结构（严格按规格给定的字段顺序）；
//    2. 中间表示 Instruction 结构（严格按规格给定的字段顺序）；
//    3. 编译错误类型 AsmError（带行号，方便定位源文件）；
//    4. 符号表条目 Symbol；
//    5. UTF-8 编解码、全角标点归一化等纯工具函数。
//
//  全部实现均为 C++17，不依赖任何第三方库。
// ============================================================================

#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace zasm {

// ----------------------------------------------------------------------------
//  COM 程序的加载基址。
//  DOS 在加载 .com 文件时，会把整个文件原样拷贝到当前段的 0x100 处，
//  因此所有绝对地址（标签、变量）都必须从 0x100 开始累加。
// ----------------------------------------------------------------------------
constexpr std::uint32_t START_ADDR = 0x100;

// ----------------------------------------------------------------------------
//  词法记号类型
// ----------------------------------------------------------------------------
enum class TokenType {
    INSTRUCTION,      // 指令助记符，如 装 / MOV
    REGISTER,         // 寄存器，如 甲 / EAX
    TYPE,             // 数据类型，如 整数 / char
    DIRECTIVE,        // 伪指令，如 入口 / 段
    IDENTIFIER,       // 标识符（标签名、变量名）
    INT_LITERAL,      // 整数字面量，如 10 / 0x1F
    CHAR_LITERAL,     // 字符字面量，如 'A'（text 中存该字符的十进制码值）
    STRING_LITERAL,   // 字符串字面量，如 "你好"（text 中存转义后的原始内容）
    PUNCT,            // 标点符号（已归一化为半角）
    NEWLINE,          // 行结束
    END               // 输入结束
};

// ----------------------------------------------------------------------------
//  词法记号（规格指定结构，字段顺序不可改变）
// ----------------------------------------------------------------------------
struct Token {
    TokenType type;      // 记号类型
    std::string text;    // 记号文本（中文词已规范化为对应的英文助记符）
    int line;            // 所在行号（从 1 开始）
};

// ----------------------------------------------------------------------------
//  中间表示：一条"语句"。
//  普通指令：opcode 为规范化的英文助记符（MOV / ADD / JMP ...）
//  伪指令 / 标签：opcode 以 '#' 开头
//      #LABEL   标签定义          operands[0] = 标签名
//      #VAR     变量声明          operands = {类型, 变量名, [初值]}
//      #ENTRY   入口声明          operands[0] = 入口标签名
//      #SEG     段声明            operands[0] = 段名（不影响布局）
//  （规格指定结构，字段顺序不可改变）
// ----------------------------------------------------------------------------
struct Instruction {
    std::string opcode;              // 伪指令/标签以 # 开头，如 "#LABEL"
    std::vector<Token> operands;     // 操作数记号（行内的逗号已被过滤掉）
    int line;                        // 所在行号
};

// ----------------------------------------------------------------------------
//  编译期错误：统一携带行号，main 中捕获后打印 "第 N 行：xxx"
// ----------------------------------------------------------------------------
class AsmError : public std::runtime_error {
public:
    AsmError(const std::string& message, int line)
        : std::runtime_error(message), line_(line) {}

    int line() const noexcept { return line_; }

private:
    int line_;
};

// ----------------------------------------------------------------------------
//  符号表条目
//
//  addr 一律是"含 0x100 基址的绝对地址"，Pass 2 回填时直接使用。
// ----------------------------------------------------------------------------
struct Symbol {
    std::string name;                  // 符号名（标签名或变量名）
    std::uint32_t addr = 0;            // 绝对地址
    std::uint32_t size = 0;            // 占用字节数；标签为 0
    bool isVar = false;                // true = 变量，false = 标签
    std::string typeName;              // 变量类型："INT" / "CHAR" / "STRING"
    std::vector<std::uint8_t> data;    // 变量的初始数据（小端序）
    int line = 0;                      // 定义所在行号
};

// 使用 std::map 而不是 unordered_map：打印符号表时天然按名字排序，输出稳定。
using SymbolTable = std::map<std::string, Symbol>;

// ----------------------------------------------------------------------------
//  UTF-8 解码：std::string（UTF-8 字节流） -> std::u32string（码点序列）
//
//  之所以先整体解码成码点，是因为后续所有操作（最长匹配中文分词、全角标点
//  归一化、字符字面量取值）都以"一个字符"为单位，逐个字节处理极易出错。
// ----------------------------------------------------------------------------
inline std::u32string utf8Decode(const std::string& text) {
    std::u32string out;
    out.reserve(text.size());

    std::size_t i = 0;
    const std::size_t n = text.size();

    while (i < n) {
        unsigned char lead = static_cast<unsigned char>(text[i]);
        std::uint32_t code = 0;
        std::size_t extra = 0;   // 后续跟随字节个数

        if (lead < 0x80) {
            code = lead;
            extra = 0;
        } else if ((lead & 0xE0) == 0xC0) {
            code = lead & 0x1F;
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            code = lead & 0x0F;
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            code = lead & 0x07;
            extra = 3;
        } else {
            // 非法首字节：按单字节原样保留，避免整个词法分析器崩溃
            code = lead;
            extra = 0;
        }

        ++i;
        for (std::size_t k = 0; k < extra && i < n; ++k, ++i) {
            unsigned char cont = static_cast<unsigned char>(text[i]);
            if ((cont & 0xC0) != 0x80) {
                break;   // 遇到非法续字节，提前结束该码点
            }
            code = (code << 6) | (cont & 0x3F);
        }

        out.push_back(static_cast<char32_t>(code));
    }

    return out;
}

// ----------------------------------------------------------------------------
//  UTF-8 编码：std::u32string（码点序列） -> std::string（UTF-8 字节流）
// ----------------------------------------------------------------------------
inline std::string utf8Encode(const std::u32string& text) {
    std::string out;
    out.reserve(text.size());

    for (char32_t cp : text) {
        std::uint32_t code = static_cast<std::uint32_t>(cp);
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    return out;
}

// ----------------------------------------------------------------------------
//  全角标点 -> 半角标点归一化
//
//  只在"字符串字面量之外"调用；字符串内部的内容必须原样保留。
// ----------------------------------------------------------------------------
inline char32_t normalizePunct(char32_t c) {
    switch (c) {
        // 逗号类：全角逗号、顿号都当作半角逗号
        case U'，':
        case U'、':
            return U',';
        case U'：':
            return U':';
        case U'；':
            return U';';
        case U'（':
            return U'(';
        case U'）':
            return U')';
        case U'［':
        case U'【':
            return U'[';
        case U'］':
        case U'】':
            return U']';
        case U'＋':
            return U'+';
        case U'－':
        case U'—':
            return U'-';
        case U'＊':
            return U'*';
        case U'／':
            return U'/';
        case U'＝':
            return U'=';
        case U'＜':
            return U'<';
        case U'＞':
            return U'>';
        case U'。':
        case U'．':
            return U'.';
        case U'！':
            return U'!';
        case U'？':
            return U'?';
        case U'“':
        case U'”':
            return U'"';
        case U'‘':
        case U'’':
            return U'\'';
        case U'\u3000':   // 全角空格
            return U' ';
        default:
            return c;
    }
}

// ----------------------------------------------------------------------------
//  判断某个（已归一化的）字符能否作为"词"的一部分。
//
//  规则：
//    * ASCII 字母 / 数字 / 下划线；
//    * 任何非 ASCII 字符（中文字符、全角字符等）。
//  标点（逗号、冒号、方括号……）不算词字符，会在词法分析里单独成 token。
// ----------------------------------------------------------------------------
inline bool isWordChar(char32_t raw) {
    char32_t c = normalizePunct(raw);
    if (c < 0x80) {
        if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z')) return true;
        if (c >= U'0' && c <= U'9') return true;
        if (c == U'_') return true;
        return false;
    }
    return true;   // 非 ASCII 一律视为词字符（由关键词表决定具体类型）
}

// ----------------------------------------------------------------------------
//  判断某个（已归一化的）字符是否为标点
// ----------------------------------------------------------------------------
inline bool isPunctChar(char32_t c) {
    switch (c) {
        case U',': case U':': case U';': case U'(': case U')':
        case U'[': case U']': case U'+': case U'-': case U'*':
        case U'/': case U'=': case U'<': case U'>': case U'!':
        case U'?': case U'.': case U'{': case U'}': case U'%':
        case U'&': case U'|': case U'^': case U'~': case U'#':
            return true;
        default:
            return false;
    }
}

// ----------------------------------------------------------------------------
//  寄存器名 -> x86 机器码编号
//
//  重要说明（与规格的对应关系）：
//    规格规定"甲=0、乙=1、丙=2、丁=3"，这正好就是 Intel 手册中 ModR/M
//    的 reg / rm 字段，以及 "50+rd"、"B8+rd" 这类 opcode 的寄存器编号：
//        0 = EAX（甲）   1 = ECX（乙）   2 = EDX（丙）   3 = EBX（丁）
//    因此 `装 甲, 乙` 的 ModR/M = 11 001 000 = C8，输出 89 C8
//    （mov eax, ecx），与规格中给出的例子完全一致。
// ----------------------------------------------------------------------------
inline int registerCode(const std::string& name) {
    if (name == "EAX") return 0;
    if (name == "ECX") return 1;
    if (name == "EDX") return 2;
    if (name == "EBX") return 3;
    return -1;
}

inline bool isRegisterName(const std::string& name) {
    return registerCode(name) >= 0;
}

// ----------------------------------------------------------------------------
//  解析整数字面量：支持十进制与 0x 前缀的十六进制。
// ----------------------------------------------------------------------------
inline std::int64_t parseIntLiteral(const std::string& text) {
    // 形式一：汇编风格的后缀写法，如 4Ch、10H
    if (text.size() > 1 && (text.back() == 'h' || text.back() == 'H')) {
        const std::string digits = text.substr(0, text.size() - 1);
        return static_cast<std::int64_t>(std::strtoll(digits.c_str(), nullptr, 16));
    }
    // 形式二：C 风格前缀写法，如 0x1F
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        return static_cast<std::int64_t>(std::strtoll(text.c_str() + 2, nullptr, 16));
    }
    // 形式三：十进制
    return static_cast<std::int64_t>(std::strtoll(text.c_str(), nullptr, 10));
}

// ----------------------------------------------------------------------------
//  把地址格式化成 "0100H" 这样的形式，仅供终端打印使用。
// ----------------------------------------------------------------------------
inline std::string formatHex(std::uint32_t value) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex
        << std::setw(4) << std::setfill('0') << value
        << "H";
    return oss.str();
}

// ----------------------------------------------------------------------------
//  构造一个记号的小工具（让各模块的代码更短）
// ----------------------------------------------------------------------------
inline Token makeToken(TokenType type, const std::string& text, int line) {
    Token token;
    token.type = type;
    token.text = text;
    token.line = line;
    return token;
}

}  // namespace zasm

#endif  // ZHASM_COMMON_H