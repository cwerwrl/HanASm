#ifndef ZHASM_PASS1_H
#define ZHASM_PASS1_H

// ============================================================================
//  Pass 1：符号表建立与地址（长度）计算
// ----------------------------------------------------------------------------
//  从 currentPC = 0x100 开始遍历中间表示：
//    #LABEL  记录标签名 -> 当前地址（不占空间），重复定义报错；
//    #VAR    记录变量名 -> 当前地址，然后按类型推进 currentPC：
//              整数 4 字节、字符 1 字节、字串 1 字节长度 + 内容；
//    指令    按固定（或最长）编码长度推进 currentPC，不做短跳优化。
//
//  长度表（已与 Pass 2 的实际编码逐条对齐）：
//    停/返/空                     1
//    系调（无参 / 带参）          2 / 4
//    跳/调                        5
//    等跳/不等跳                  6
//    压/弹                        1
//    加/减/比                     2
//    自增                        1（40+rd 实际只占 1 字节）
//    装 寄存器, 立即数            5
//    装 寄存器, 寄存器            2
//    装 寄存器, [寄存器]          3（强制 mod=01 + disp8=0，见 Pass 2 说明）
//    装 寄存器, [寄存器+偏移]     3
//    装 寄存器, 标签              5
//    装 [寄存器], 寄存器          3
//    装 [寄存器], 立即数          7
// ============================================================================

#include "common.h"

namespace zasm {

// ----------------------------------------------------------------------------
//  Pass 1 的产物
// ----------------------------------------------------------------------------
struct Pass1Result {
    SymbolTable symbols;         // 符号表（标签 + 变量）
    std::uint32_t endAddr = START_ADDR;  // 全部内容结束后的地址
    std::string entryLabel;      // 入口标签（可能为空）
    bool hasEntry = false;       // 是否声明了入口
    int entryLine = 0;           // 入口声明所在行
};

// ----------------------------------------------------------------------------
//  生成变量的初始数据（整数按小端序，字串 = 1 字节长度 + 内容）
// ----------------------------------------------------------------------------
std::vector<std::uint8_t> buildVariableData(const std::string& typeName,
                                            const Token* value,
                                            int line);

// 变量占用字节数
std::uint32_t variableSize(const std::string& typeName, const Token* value, int line);

// 普通指令占用字节数（伪指令返回 0 或数据大小）
std::uint32_t instructionSize(const Instruction& ins);

// 执行 Pass 1
Pass1Result runPass1(const std::vector<Instruction>& program);

}  // namespace zasm

#endif  // ZHASM_PASS1_H