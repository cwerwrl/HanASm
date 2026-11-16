// ============================================================================
//  Pass 2 实现：机器码生成与符号回填
// ============================================================================

#include "pass2.h"

#include "parser.h"
#include "pass1.h"

#include <string>
#include <vector>

namespace zasm {

namespace {

// ----------------------------------------------------------------------------
//  输出辅助
// ----------------------------------------------------------------------------

// 写一个字节
void emitByte(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

// 写一个 32 位小端序数值
void emitDword(std::vector<std::uint8_t>& out, std::uint32_t value) {
    emitByte(out, value & 0xFF);
    emitByte(out, (value >> 8) & 0xFF);
    emitByte(out, (value >> 16) & 0xFF);
    emitByte(out, (value >> 24) & 0xFF);
}

// 构造 ModR/M 字节
//   mod: 2 位；reg: 3 位；r/m: 3 位
std::uint8_t modrm(int mod, int reg, int rm) {
    return static_cast<std::uint8_t>(((mod & 0x3) << 6) |
                                     ((reg & 0x7) << 3) |
                                     (rm & 0x7));
}

// [寄存器 + 偏移] 的 disp8
//   统一使用 mod=01，因此 [寄存器] 也要写一个 disp8=0，
//   这样能彻底避开 x86 的 "mod=00 且 r/m=101 表示 disp32" 的坑。
std::uint8_t disp8Of(const Operand& op, int line) {
    if (op.kind == OperandKind::MEM_REG) {
        return 0;
    }
    if (op.imm < -128 || op.imm > 127) {
        throw AsmError("偏移量超出 8 位有符号范围（-128..127）", line);
    }
    return static_cast<std::uint8_t>(static_cast<std::int8_t>(op.imm));
}

// 解析跳转 / 调用的目标地址（标签查符号表，立即数直接使用）
std::uint32_t resolveTarget(const Operand& op, const SymbolTable& symbols, int line) {
    if (op.kind == OperandKind::LABEL) {
        auto it = symbols.find(op.text);
        if (it == symbols.end()) {
            throw AsmError("未定义的符号：" + op.text, line);
        }
        return it->second.addr;
    }

    if (op.kind == OperandKind::IMM) {
        return static_cast<std::uint32_t>(op.imm);
    }

    throw AsmError("跳转目标必须是标签或绝对地址", line);
}

// ----------------------------------------------------------------------------
//  传送指令编码
//
//  编码形式与 Pass 1 的长度表一一对应：
//    寄存器, 寄存器        89 /r              2 字节
//    寄存器, 立即数        B8+rd id           5 字节
//    寄存器, 标签          B8+rd id（回填）   5 字节
//    寄存器, [寄存器]      8B /r disp8=0      3 字节
//    寄存器, [寄存器+偏移] 8B /r disp8        3 字节
//    [寄存器], 寄存器      89 /r disp8=0      3 字节
//    [寄存器+偏移], 寄存器 89 /r disp8        3 字节
//    [寄存器], 立即数      C7 /0 disp8 id     7 字节
// ----------------------------------------------------------------------------
void encodeMov(const Instruction& ins,
               const std::vector<Operand>& ops,
               const SymbolTable& symbols,
               std::vector<std::uint8_t>& out) {
    const Operand& dst = ops[0];
    const Operand& src = ops[1];

    // ---------------- 目的操作数是寄存器 ----------------
    if (dst.kind == OperandKind::REG) {
        const int rd = dst.reg;

        // 89 /r：reg 字段 = 源寄存器，r/m 字段 = 目的寄存器
        // 例：装 甲, 乙 -> 89 C8（mov eax, ecx）
        if (src.kind == OperandKind::REG) {
            emitByte(out, 0x89);
            emitByte(out, modrm(3, src.reg, rd));
            return;
        }

        // B8+rd：装载 32 位立即数
        if (src.kind == OperandKind::IMM) {
            emitByte(out, static_cast<std::uint32_t>(0xB8 + rd));
            emitDword(out, static_cast<std::uint32_t>(src.imm));
            return;
        }

        // B8+rd + 符号地址：把标签/变量地址作为立即数装入
        if (src.kind == OperandKind::LABEL) {
            emitByte(out, static_cast<std::uint32_t>(0xB8 + rd));
            emitDword(out, resolveTarget(src, symbols, ins.line));
            return;
        }

        // 8B /r：从内存读取
        if (src.kind == OperandKind::MEM_REG || src.kind == OperandKind::MEM_REG_DISP) {
            emitByte(out, 0x8B);
            emitByte(out, modrm(1, rd, src.reg));
            emitByte(out, disp8Of(src, ins.line));
            return;
        }

        throw AsmError("装(MOV) 不支持该源操作数", ins.line);
    }

    // ---------------- 目的操作数是内存 ----------------
    if (dst.kind == OperandKind::MEM_REG || dst.kind == OperandKind::MEM_REG_DISP) {
        // 89 /r：把寄存器写入内存，reg 字段 = 源寄存器
        if (src.kind == OperandKind::REG) {
            emitByte(out, 0x89);
            emitByte(out, modrm(1, src.reg, dst.reg));
            emitByte(out, disp8Of(dst, ins.line));
            return;
        }

        // C7 /0：把立即数写入内存（寄存器间接寻址专用）
        if (src.kind == OperandKind::IMM) {
            emitByte(out, 0xC7);
            emitByte(out, modrm(1, 0, dst.reg));
            emitByte(out, disp8Of(dst, ins.line));
            emitDword(out, static_cast<std::uint32_t>(src.imm));
            return;
        }

        throw AsmError("装(MOV) 不支持该源操作数", ins.line);
    }

    throw AsmError("装(MOV) 的第一个操作数必须是寄存器或内存", ins.line);
}

// ----------------------------------------------------------------------------
//  单条指令编码
//  pc 为该指令在内存中的绝对地址（含 0x100 基址），用于计算相对位移。
// ----------------------------------------------------------------------------
void encodeInstruction(const Instruction& ins,
                       std::uint32_t pc,
                       const SymbolTable& symbols,
                       std::vector<std::uint8_t>& out) {
    const std::string& op = ins.opcode;

    // ---------------- 无操作数指令 ----------------
    if (op == "HLT") { emitByte(out, 0xF4); return; }
    if (op == "NOP") { emitByte(out, 0x90); return; }
    if (op == "RET") { emitByte(out, 0xC3); return; }

    const std::vector<Operand> ops = parseOperandList(ins.operands, ins.line);

    // ---------------- 系统调用 ----------------
    if (op == "INT") {
        if (ops.empty()) {
            emitByte(out, 0xCD);   // INT 21H
            emitByte(out, 0x21);
            return;
        }
        if (ops.size() != 1 || ops[0].kind != OperandKind::IMM) {
            throw AsmError("系调只接受一个立即数中断号", ins.line);
        }
        if (ops[0].imm < 0 || ops[0].imm > 255) {
            throw AsmError("中断功能号必须在 0..255 之间", ins.line);
        }
        emitByte(out, 0xB4);                            // MOV AH, imm8
        emitByte(out, static_cast<std::uint32_t>(ops[0].imm));
        emitByte(out, 0xCD);                            // INT 21H
        emitByte(out, 0x21);
        return;
    }

    // ---------------- 压栈 / 弹栈 / 自增 ----------------
    if (op == "PUSH" || op == "POP" || op == "INC") {
        if (ops.size() != 1 || ops[0].kind != OperandKind::REG) {
            throw AsmError(op + " 需要一个寄存器操作数", ins.line);
        }
        const int rd = ops[0].reg;
        if (op == "PUSH") {
            emitByte(out, static_cast<std::uint32_t>(0x50 + rd));
        } else if (op == "POP") {
            emitByte(out, static_cast<std::uint32_t>(0x58 + rd));
        } else {
            emitByte(out, static_cast<std::uint32_t>(0x40 + rd));
        }
        return;
    }

    // ---------------- 加 / 减 / 比：03 / 2B / 3B /r ----------------
    if (op == "ADD" || op == "SUB" || op == "CMP") {
        if (ops.size() != 2 ||
            ops[0].kind != OperandKind::REG ||
            ops[1].kind != OperandKind::REG) {
            throw AsmError(op + " 只支持“寄存器, 寄存器”形式", ins.line);
        }
        std::uint32_t opcode = 0x03;
        if (op == "SUB") opcode = 0x2B;
        if (op == "CMP") opcode = 0x3B;

        // 03 /r：reg 字段 = 目的寄存器，r/m 字段 = 源寄存器
        // 例：加 甲, 乙 -> 03 C1（add eax, ecx）
        emitByte(out, opcode);
        emitByte(out, modrm(3, ops[0].reg, ops[1].reg));
        return;
    }

    // ---------------- 跳转 / 调用：E9 / E8 + rel32 ----------------
    if (op == "JMP" || op == "CALL") {
        if (ops.size() != 1) {
            throw AsmError(op + " 需要一个跳转目标", ins.line);
        }
        const std::uint32_t target = resolveTarget(ops[0], symbols, ins.line);
        // rel32 = 目标地址 - (当前指令地址 + 5)
        const std::int64_t rel = static_cast<std::int64_t>(target) -
                                 static_cast<std::int64_t>(pc + 5);
        if (rel < -2147483647LL - 1 || rel > 2147483647LL) {
            throw AsmError("跳转距离超出 32 位范围", ins.line);
        }
        emitByte(out, op == "JMP" ? 0xE9 : 0xE8);
        emitDword(out, static_cast<std::uint32_t>(rel));
        return;
    }

    // ---------------- 条件跳转：0F 84 / 85 + rel32 ----------------
    if (op == "JE" || op == "JNE") {
        if (ops.size() != 1) {
            throw AsmError(op + " 需要一个跳转目标", ins.line);
        }
        const std::uint32_t target = resolveTarget(ops[0], symbols, ins.line);
        // rel32 = 目标地址 - (当前指令地址 + 6)
        const std::int64_t rel = static_cast<std::int64_t>(target) -
                                 static_cast<std::int64_t>(pc + 6);
        if (rel < -2147483647LL - 1 || rel > 2147483647LL) {
            throw AsmError("跳转距离超出 32 位范围", ins.line);
        }
        emitByte(out, 0x0F);
        emitByte(out, op == "JE" ? 0x84 : 0x85);
        emitDword(out, static_cast<std::uint32_t>(rel));
        return;
    }

    // ---------------- 传送 ----------------
    if (op == "MOV") {
        if (ops.size() != 2) {
            throw AsmError("装(MOV) 需要两个操作数", ins.line);
        }
        encodeMov(ins, ops, symbols, out);
        return;
    }

    throw AsmError("未实现的指令：" + op, ins.line);
}

}  // namespace

// ----------------------------------------------------------------------------
//  Pass 2 主流程
// ----------------------------------------------------------------------------
std::vector<std::uint8_t> runPass2(const std::vector<Instruction>& program,
                                   const SymbolTable& symbols) {
    std::vector<std::uint8_t> output;

    for (const Instruction& ins : program) {
        // 当前指令地址 = 基址 + 已输出字节数
        const std::uint32_t pc = START_ADDR + static_cast<std::uint32_t>(output.size());

        // ---------------- 标签：只做一致性校验 ----------------
        if (ins.opcode == "#LABEL") {
            auto it = symbols.find(ins.operands[0].text);
            if (it == symbols.end() || it->second.addr != pc) {
                throw AsmError("内部错误：标签地址与 Pass 1 不一致：" +
                               ins.operands[0].text, ins.line);
            }
            continue;
        }

        // ---------------- 变量：把初始数据原样写入输出 ----------------
        if (ins.opcode == "#VAR") {
            auto it = symbols.find(ins.operands[1].text);
            if (it == symbols.end() || it->second.addr != pc) {
                throw AsmError("内部错误：变量地址与 Pass 1 不一致：" +
                               ins.operands[1].text, ins.line);
            }
            output.insert(output.end(), it->second.data.begin(), it->second.data.end());
            continue;
        }

        // ---------------- 入口 / 段：不产生字节 ----------------
        if (ins.opcode == "#ENTRY" || ins.opcode == "#SEG") {
            continue;
        }

        // ---------------- 普通指令 ----------------
        const std::size_t before = output.size();
        encodeInstruction(ins, pc, symbols, output);

        // 安全网：Pass 2 的实际字节数必须与 Pass 1 的长度完全一致，
        // 否则后续所有标签地址都会错位。
        const std::uint32_t expected = instructionSize(ins);
        if (output.size() - before != expected) {
            throw AsmError("内部错误：指令编码长度与 Pass 1 不一致（" +
                           ins.opcode + "，期望 " + std::to_string(expected) +
                           " 字节，实际 " +
                           std::to_string(output.size() - before) + " 字节）",
                           ins.line);
        }
    }

    return output;
}

}  // namespace zasm