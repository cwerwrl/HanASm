#ifndef ZHASM_PASS2_H
#define ZHASM_PASS2_H

// ============================================================================
//  Pass 2：机器码生成与回填
// ----------------------------------------------------------------------------
//  再次遍历中间表示，从 0x100 开始逐条生成机器码：
//    * 标签、变量使用 Pass 1 的符号表得到绝对地址；
//    * 跳转使用 rel32 回填，因此没有"短跳优化"，长度与 Pass 1 完全一致；
//    * 遇到未实现的编码组合立即抛出 AsmError。
//
//  ModR/M 的寄存器编号与规格一致：甲=0(EAX) 乙=1(ECX) 丙=2(EDX) 丁=3(EBX)。
// ============================================================================

#include "common.h"

namespace zasm {

// 生成最终的 COM 映像（不含 DOS 头部，直接从 0x100 处的第一条指令开始）
std::vector<std::uint8_t> runPass2(const std::vector<Instruction>& program,
                                   const SymbolTable& symbols);

}  // namespace zasm

#endif  // ZHASM_PASS2_H