#ifndef ZHASM_PARSER_H
#define ZHASM_PARSER_H

// ============================================================================
//  语法分析器
// ----------------------------------------------------------------------------
//  职责（把 Token 流变成 Instruction 流）：
//    1. 标签：        loop:            -> #LABEL      operands = {loop}
//    2. 伪指令：      入口 开始 / 段 代码 -> #ENTRY / #SEG
//    3. 变量声明：    整数 计数 = 10     -> #VAR operands = {整数, 计数, 10}
//    4. 普通指令：    装 甲, 10         -> MOV operands = {甲, 10}
//  行内的逗号只是操作数分隔符，不会进入 operands。
// ============================================================================

#include "common.h"

namespace zasm {

// ----------------------------------------------------------------------------
//  操作数的"逻辑种类"（Pass1 计算长度、Pass2 选择编码形式时使用）
// ----------------------------------------------------------------------------
enum class OperandKind {
    REG,           // 寄存器              甲 / EAX
    IMM,           // 立即数              10 / 0x1F / 'A' / -1
    LABEL,         // 符号引用            循环 / 计数
    MEM_REG,       // 间接寻址            [乙]
    MEM_REG_DISP,  // 基址 + 偏移         [乙 + 4]
    STRING         // 字符串（只用于变量初值）
};

// ----------------------------------------------------------------------------
//  解析后的操作数
// ----------------------------------------------------------------------------
struct Operand {
    OperandKind kind = OperandKind::IMM;
    int reg = -1;              // REG / MEM_*：x86 寄存器编号（0..3）
    std::int64_t imm = 0;      // IMM：立即数；MEM_REG_DISP：偏移量
    std::string text;          // LABEL：符号名；STRING：字符串内容
    int line = 0;              // 所在行号
};

// ----------------------------------------------------------------------------
//  把一个语句的记号列表（已去掉逗号）解析成结构化操作数
// ----------------------------------------------------------------------------
std::vector<Operand> parseOperandList(const std::vector<Token>& tokens, int defaultLine);

// ----------------------------------------------------------------------------
//  语法分析器
// ----------------------------------------------------------------------------
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // 逐行解析，返回中间表示（顺序与源码一致）
    std::vector<Instruction> parse();

private:
    std::vector<Token> tokens_;

    // 解析"一行"（已按 NEWLINE 切分好的记号）
    void parseLine(const std::vector<Token>& line, std::vector<Instruction>& out);
};

}  // namespace zasm

#endif  // ZHASM_PARSER_H