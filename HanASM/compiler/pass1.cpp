// ============================================================================
//  Pass 1 实现
// ============================================================================

#include "pass1.h"

#include "parser.h"

#include <string>
#include <vector>

namespace zasm {

namespace {

// COM 文件最大长度：0x100 .. 0xFFFF，共 0xFF00 字节
constexpr std::uint32_t MAX_COM_SIZE = 0xFF00;

// 检查 8 位有符号偏移是否越界（[寄存器+偏移] 使用 disp8）
void checkDisp8(std::int64_t disp, int line) {
    if (disp < -128 || disp > 127) {
        throw AsmError("偏移量超出 8 位有符号范围（-128..127）：" + std::to_string(disp), line);
    }
}

// 取一个变量的初值（没有写初值时按 0 处理）
std::int64_t variableInitValue(const Token* value, int line) {
    if (value == nullptr) return 0;
    if (value->type == TokenType::STRING_LITERAL) {
        throw AsmError("整数 / 字符变量不能用字符串初始化", line);
    }
    return parseIntLiteral(value->text);
}

}  // namespace

// ----------------------------------------------------------------------------
//  变量初始数据
// ----------------------------------------------------------------------------
std::vector<std::uint8_t> buildVariableData(const std::string& typeName,
                                            const Token* value,
                                            int line) {
    // ---------------- 整数：4 字节小端序 ----------------
    if (typeName == "INT") {
        const std::int64_t v = variableInitValue(value, line);
        const std::uint32_t u = static_cast<std::uint32_t>(v);
        return {
            static_cast<std::uint8_t>(u & 0xFF),
            static_cast<std::uint8_t>((u >> 8) & 0xFF),
            static_cast<std::uint8_t>((u >> 16) & 0xFF),
            static_cast<std::uint8_t>((u >> 24) & 0xFF),
        };
    }

    // ---------------- 字符：1 字节 ----------------
    if (typeName == "CHAR") {
        const std::int64_t v = variableInitValue(value, line);
        return {static_cast<std::uint8_t>(v & 0xFF)};
    }

    // ---------------- 字串：1 字节长度 + 内容 ----------------
    if (typeName == "STRING") {
        const std::string text = (value != nullptr) ? value->text : std::string();
        if (text.size() > 255) {
            throw AsmError("字串变量超过 255 字节，无法用 1 字节保存长度", line);
        }
        std::vector<std::uint8_t> data;
        data.reserve(text.size() + 1);
        data.push_back(static_cast<std::uint8_t>(text.size()));
        data.insert(data.end(), text.begin(), text.end());
        return data;
    }

    throw AsmError("未知的数据类型：" + typeName + "（只支持 整数 / 字符 / 字串）", line);
}

std::uint32_t variableSize(const std::string& typeName, const Token* value, int line) {
    return static_cast<std::uint32_t>(buildVariableData(typeName, value, line).size());
}

// ----------------------------------------------------------------------------
//  指令长度（必须以 Pass 2 实际写出的字节数为准）
// ----------------------------------------------------------------------------
std::uint32_t instructionSize(const Instruction& ins) {
    const std::string& op = ins.opcode;

    // 不占代码空间的伪指令
    if (op == "#LABEL" || op == "#ENTRY" || op == "#SEG") {
        return 0;
    }

    // 变量声明：占用的是数据空间
    if (op == "#VAR") {
        if (ins.operands.size() < 2) {
            throw AsmError("#VAR 缺少类型或变量名", ins.line);
        }
        const Token* value = (ins.operands.size() >= 3) ? &ins.operands[2] : nullptr;
        return variableSize(ins.operands[0].text, value, ins.line);
    }

    const std::vector<Operand> ops = parseOperandList(ins.operands, ins.line);

    // ---------------- 无操作数：停 / 空 / 返 ----------------
    if (op == "HLT" || op == "NOP" || op == "RET") {
        if (!ops.empty()) {
            throw AsmError(op + " 不接受操作数", ins.line);
        }
        return 1;
    }

    // ---------------- 系统调用：CD 21 / B4 imm8 + CD 21 ----------------
    if (op == "INT") {
        if (ops.empty()) {
            return 2;
        }
        if (ops.size() != 1 || ops[0].kind != OperandKind::IMM) {
            throw AsmError("系调只接受一个立即数中断号", ins.line);
        }
        return 4;
    }

    // ---------------- 跳转 / 调用：E9/E8 + rel32 ----------------
    if (op == "JMP" || op == "CALL") {
        if (ops.size() != 1) {
            throw AsmError(op + " 需要一个跳转目标", ins.line);
        }
        return 5;
    }

    // ---------------- 条件跳转：0F 84/85 + rel32 ----------------
    if (op == "JE" || op == "JNE") {
        if (ops.size() != 1) {
            throw AsmError(op + " 需要一个跳转目标", ins.line);
        }
        return 6;
    }

    // ---------------- 栈操作：50+rd / 58+rd ----------------
    if (op == "PUSH" || op == "POP") {
        if (ops.size() != 1 || ops[0].kind != OperandKind::REG) {
            throw AsmError(op + " 需要一个寄存器操作数", ins.line);
        }
        return 1;
    }

    // ---------------- 自增：40+rd（1 字节） ----------------
    if (op == "INC") {
        if (ops.size() != 1 || ops[0].kind != OperandKind::REG) {
            throw AsmError("自增需要一个寄存器操作数", ins.line);
        }
        return 1;
    }

    // ---------------- 寄存器算术 / 比较：2 字节 ----------------
    if (op == "ADD" || op == "SUB" || op == "CMP") {
        if (ops.size() != 2 ||
            ops[0].kind != OperandKind::REG ||
            ops[1].kind != OperandKind::REG) {
            throw AsmError(op + " 只支持“寄存器, 寄存器”形式", ins.line);
        }
        return 2;
    }

    // ---------------- 传送 ----------------
    if (op == "MOV") {
        if (ops.size() != 2) {
            throw AsmError("装(MOV) 需要两个操作数", ins.line);
        }

        const Operand& dst = ops[0];
        const Operand& src = ops[1];

        // 目的操作数是寄存器
        if (dst.kind == OperandKind::REG) {
            switch (src.kind) {
                case OperandKind::REG:      return 2;   // 89 /r
                case OperandKind::IMM:      return 5;   // B8+rd id
                case OperandKind::LABEL:    return 5;   // B8+rd id（地址回填）
                case OperandKind::MEM_REG:  return 3;   // 8B /r + disp8
                case OperandKind::MEM_REG_DISP:
                    checkDisp8(src.imm, ins.line);
                    return 3;
                default:
                    break;
            }
            throw AsmError("装(MOV) 不支持该源操作数", ins.line);
        }

        // 目的操作数是内存
        if (dst.kind == OperandKind::MEM_REG || dst.kind == OperandKind::MEM_REG_DISP) {
            if (dst.kind == OperandKind::MEM_REG_DISP) {
                checkDisp8(dst.imm, ins.line);
            }
            if (src.kind == OperandKind::REG) return 3;   // 89 /r + disp8
            if (src.kind == OperandKind::IMM) return 7;   // C7 /0 + disp8 + id
            throw AsmError("装(MOV) 不支持该源操作数", ins.line);
        }

        throw AsmError("装(MOV) 的第一个操作数必须是寄存器或内存", ins.line);
    }

    throw AsmError("未实现的指令：" + op, ins.line);
}

// ----------------------------------------------------------------------------
//  Pass 1 主流程
// ----------------------------------------------------------------------------
Pass1Result runPass1(const std::vector<Instruction>& program) {
    Pass1Result result;
    std::uint32_t pc = START_ADDR;

    for (const Instruction& ins : program) {
        // ---------------- 标签 ----------------
        if (ins.opcode == "#LABEL") {
            if (ins.operands.empty()) {
                throw AsmError("标签缺少名字", ins.line);
            }
            const std::string& name = ins.operands[0].text;

            if (result.symbols.find(name) != result.symbols.end()) {
                throw AsmError("重复定义的符号：" + name, ins.line);
            }

            Symbol sym;
            sym.name = name;
            sym.addr = pc;
            sym.isVar = false;
            sym.size = 0;
            sym.line = ins.line;
            result.symbols.emplace(name, sym);
            continue;
        }

        // ---------------- 变量声明 ----------------
        if (ins.opcode == "#VAR") {
            if (ins.operands.size() < 2) {
                throw AsmError("变量声明缺少类型或变量名", ins.line);
            }
            const std::string& typeName = ins.operands[0].text;
            const std::string& name = ins.operands[1].text;

            if (result.symbols.find(name) != result.symbols.end()) {
                throw AsmError("重复定义的符号：" + name, ins.line);
            }

            const Token* value = (ins.operands.size() >= 3) ? &ins.operands[2] : nullptr;

            Symbol sym;
            sym.name = name;
            sym.addr = pc;
            sym.isVar = true;
            sym.typeName = typeName;
            sym.data = buildVariableData(typeName, value, ins.line);
            sym.size = static_cast<std::uint32_t>(sym.data.size());
            sym.line = ins.line;

            result.symbols.emplace(name, sym);
            pc += sym.size;
            continue;
        }

        // ---------------- 入口 / 段 ----------------
        if (ins.opcode == "#ENTRY") {
            if (!ins.operands.empty()) {
                result.entryLabel = ins.operands[0].text;
                result.hasEntry = true;
                result.entryLine = ins.line;
            }
            continue;
        }

        if (ins.opcode == "#SEG") {
            continue;   // 段只用于阅读，不影响 COM 的平坦布局
        }

        // ---------------- 普通指令 ----------------
        pc += instructionSize(ins);

        if (pc - START_ADDR > MAX_COM_SIZE) {
            throw AsmError("生成的 COM 程序超过 65280 字节（0x100..0xFFFF）", ins.line);
        }
    }

    // 校验入口标签是否真实存在
    if (result.hasEntry &&
        result.symbols.find(result.entryLabel) == result.symbols.end()) {
        throw AsmError("入口标签未定义：" + result.entryLabel, result.entryLine);
    }

    result.endAddr = pc;
    return result;
}

}  // namespace zasm