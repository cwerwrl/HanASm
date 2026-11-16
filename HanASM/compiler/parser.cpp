// ============================================================================
//  语法分析器实现
// ============================================================================

#include "parser.h"

#include <utility>

namespace zasm {

namespace {

// 判断一个记号是否为指定标点
bool isPunct(const Token& token, const char* text) {
    return token.type == TokenType::PUNCT && token.text == text;
}

// ----------------------------------------------------------------------------
//  解析单个操作数
//
//  支持的形式：
//    寄存器              甲
//    立即数              10 / 0x1F / 'A' / -1
//    符号                循环 / 计数
//    字符串              "你好"
//    内存                [乙] / [乙 + 4] / [4 + 乙]
// ----------------------------------------------------------------------------
Operand parseOneOperand(const std::vector<Token>& group, int defaultLine) {
    if (group.empty()) {
        throw AsmError("空操作数", defaultLine);
    }

    const int line = group.front().line;

    // ---------------- 内存操作数：以 [ 开头 ----------------
    if (isPunct(group.front(), "[")) {
        if (group.size() < 2 || !isPunct(group.back(), "]")) {
            throw AsmError("内存操作数缺少右方括号 ]", line);
        }

        std::vector<Token> inner(group.begin() + 1, group.end() - 1);
        if (inner.empty()) {
            throw AsmError("[] 内不能为空", line);
        }

        Operand op;
        op.line = line;

        // [寄存器]
        if (inner.size() == 1 && inner[0].type == TokenType::REGISTER) {
            op.kind = OperandKind::MEM_REG;
            op.reg = registerCode(inner[0].text);
            return op;
        }

        // [寄存器 + 偏移] 或 [偏移 + 寄存器]
        if (inner.size() == 3 && isPunct(inner[1], "+")) {
            const Token* regTok = nullptr;
            const Token* immTok = nullptr;

            if (inner[0].type == TokenType::REGISTER && inner[2].type == TokenType::INT_LITERAL) {
                regTok = &inner[0];
                immTok = &inner[2];
            } else if (inner[0].type == TokenType::INT_LITERAL && inner[2].type == TokenType::REGISTER) {
                regTok = &inner[2];
                immTok = &inner[0];
            }

            if (regTok != nullptr && immTok != nullptr) {
                op.kind = OperandKind::MEM_REG_DISP;
                op.reg = registerCode(regTok->text);
                op.imm = parseIntLiteral(immTok->text);
                return op;
            }
        }

        throw AsmError("不支持的内存寻址形式（只支持 [寄存器] 和 [寄存器+偏移]）", line);
    }

    // ---------------- 单个记号 ----------------
    if (group.size() == 1) {
        const Token& token = group[0];
        Operand op;
        op.line = line;

        switch (token.type) {
            case TokenType::REGISTER:
                op.kind = OperandKind::REG;
                op.reg = registerCode(token.text);
                return op;

            case TokenType::INT_LITERAL:
                op.kind = OperandKind::IMM;
                op.imm = parseIntLiteral(token.text);
                return op;

            case TokenType::CHAR_LITERAL:
                // 字符字面量在词法阶段已转成十进制码值
                op.kind = OperandKind::IMM;
                op.imm = parseIntLiteral(token.text);
                return op;

            case TokenType::IDENTIFIER:
                op.kind = OperandKind::LABEL;
                op.text = token.text;
                return op;

            case TokenType::STRING_LITERAL:
                op.kind = OperandKind::STRING;
                op.text = token.text;
                return op;

            default:
                throw AsmError("不能作为操作数：" + token.text, line);
        }
    }

    // ---------------- 负数立即数：-10 ----------------
    if (group.size() == 2 && isPunct(group[0], "-") &&
        group[1].type == TokenType::INT_LITERAL) {
        Operand op;
        op.kind = OperandKind::IMM;
        op.imm = -parseIntLiteral(group[1].text);
        op.line = line;
        return op;
    }

    throw AsmError("无法解析的操作数", line);
}

}  // namespace

// ----------------------------------------------------------------------------
//  操作数列表：逗号是分隔符，其余标点（+ [ ] - 等）留在组内
// ----------------------------------------------------------------------------
std::vector<Operand> parseOperandList(const std::vector<Token>& tokens, int defaultLine) {
    std::vector<Operand> result;
    std::vector<Token> group;

    for (const Token& token : tokens) {
        if (isPunct(token, ",")) {
            if (group.empty()) {
                throw AsmError("操作数列表中出现多余的逗号", token.line);
            }
            result.push_back(parseOneOperand(group, defaultLine));
            group.clear();
        } else {
            group.push_back(token);
        }
    }

    if (!group.empty()) {
        result.push_back(parseOneOperand(group, defaultLine));
    }

    return result;
}

// ----------------------------------------------------------------------------
//  Parser 构造
// ----------------------------------------------------------------------------
Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

// ----------------------------------------------------------------------------
//  主循环：按 NEWLINE 切行，再逐行解析
// ----------------------------------------------------------------------------
std::vector<Instruction> Parser::parse() {
    std::vector<Instruction> program;
    std::vector<Token> line;

    for (const Token& token : tokens_) {
        if (token.type == TokenType::NEWLINE) {
            if (!line.empty()) {
                parseLine(line, program);
                line.clear();
            }
            continue;
        }

        if (token.type == TokenType::END) {
            if (!line.empty()) {
                parseLine(line, program);
                line.clear();
            }
            break;
        }

        line.push_back(token);
    }

    if (!line.empty()) {
        parseLine(line, program);
    }

    return program;
}

// ----------------------------------------------------------------------------
//  解析一行
// ----------------------------------------------------------------------------
void Parser::parseLine(const std::vector<Token>& line, std::vector<Instruction>& out) {
    std::size_t idx = 0;

    // 先允许出现若干个"标签："前缀（也支持 循环: 装 甲, 1 这种写法）
    while (idx + 1 < line.size() &&
           line[idx].type == TokenType::IDENTIFIER &&
           isPunct(line[idx + 1], ":")) {
        Instruction label;
        label.opcode = "#LABEL";
        label.line = line[idx].line;
        label.operands.push_back(line[idx]);
        out.push_back(label);
        idx += 2;
    }

    if (idx >= line.size()) {
        return;   // 整行只有标签
    }

    const Token& head = line[idx];

    // ---------------- 伪指令：入口 / 段 ----------------
    if (head.type == TokenType::DIRECTIVE) {
        Instruction dir;
        dir.opcode = head.text;     // "#ENTRY" 或 "#SEG"
        dir.line = head.line;
        // 逗号保留在操作数序列中，由 parseOperandList 统一当作分隔符处理
        for (std::size_t i = idx + 1; i < line.size(); ++i) {
            dir.operands.push_back(line[i]);
        }
        out.push_back(dir);
        return;
    }

    // ---------------- 普通指令 ----------------
    if (head.type == TokenType::INSTRUCTION) {
        Instruction ins;
        ins.opcode = head.text;
        ins.line = head.line;
        // 逗号保留在操作数序列中，由 parseOperandList 统一当作分隔符处理；
        // 这样 "[乙 + 4]" 里的 '+' 就不会被误当作操作数分隔符。
        for (std::size_t i = idx + 1; i < line.size(); ++i) {
            ins.operands.push_back(line[i]);
        }
        out.push_back(ins);
        return;
    }

    // ---------------- 变量声明：类型 名字 [= 值] ----------------
    if (head.type == TokenType::TYPE) {
        if (idx + 1 >= line.size()) {
            throw AsmError("变量声明缺少变量名", head.line);
        }

        const Token& nameTok = line[idx + 1];
        // 注意：中文变量名有可能与寄存器同名（如规格示例里的 "整数 甲 = 10"），
        //       因此 REGISTER 记号也允许作为变量名。
        if (nameTok.type != TokenType::IDENTIFIER &&
            nameTok.type != TokenType::REGISTER) {
            throw AsmError("变量名必须是标识符", nameTok.line);
        }

        Instruction var;
        var.opcode = "#VAR";
        var.line = head.line;
        var.operands.push_back(head);      // operands[0] = 类型
        var.operands.push_back(nameTok);   // operands[1] = 变量名

        std::size_t j = idx + 2;
        if (j < line.size()) {
            if (!isPunct(line[j], "=")) {
                throw AsmError("变量声明中缺少 =", line[j].line);
            }
            ++j;
            if (j >= line.size()) {
                throw AsmError("= 后面缺少初值", head.line);
            }

            Token value = line[j];

            // 处理负数初值：= -1
            if (value.type == TokenType::PUNCT && value.text == "-" &&
                j + 1 < line.size() && line[j + 1].type == TokenType::INT_LITERAL) {
                value = line[j + 1];
                value.text = "-" + value.text;
                ++j;
            }

            if (value.type != TokenType::INT_LITERAL &&
                value.type != TokenType::CHAR_LITERAL &&
                value.type != TokenType::STRING_LITERAL) {
                throw AsmError("变量初值必须是整数、字符或字符串", value.line);
            }

            var.operands.push_back(value);
            ++j;

            if (j < line.size()) {
                throw AsmError("变量声明后面存在多余内容", line[j].line);
            }
        }

        out.push_back(var);
        return;
    }

    throw AsmError("无法识别的语句开头：" + head.text, head.line);
}

}  // namespace zasm