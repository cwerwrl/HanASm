#ifndef ZHASM_LEXER_H
#define ZHASM_LEXER_H

// ============================================================================
//  词法分析器
// ----------------------------------------------------------------------------
//  职责：
//    1. 把 UTF-8 源文件解码成码点序列，支持中文；
//    2. 识别中文 / 英文的指令、寄存器、类型、伪指令（大小写不敏感）；
//    3. 中文按"最长匹配"分词，保证 "不等跳" 不会被拆成 "不等" + "跳"；
//    4. 全角标点自动归一化为半角（字符串内部保持原样）；
//    5. 支持 ; 、// 、/* */ 、注 四种注释写法。
//
//  输出：std::vector<Token>，行与行之间用 NEWLINE 记号分隔。
// ============================================================================

#include "common.h"

namespace zasm {

class Lexer {
public:
    // 构造时完成 UTF-8 解码，并去掉可能存在的 BOM
    explicit Lexer(std::string source);

    // 扫描整个源文件，返回记号序列（末尾一定带一个 END 记号）
    std::vector<Token> run();

private:
    std::u32string src_;    // 源码的码点序列
    std::size_t pos_ = 0;   // 当前扫描位置（码点下标）
    int line_ = 1;          // 当前行号

    bool atEnd() const { return pos_ >= src_.size(); }

    // 读取指定偏移处的原始字符 / 归一化后字符
    char32_t peekRaw(std::size_t offset = 0) const;
    char32_t peekNorm(std::size_t offset = 0) const;

    // 跳过空白与注释（不跨越换行；换行由 run() 生成 NEWLINE 记号）
    void skipTrivia();

    // 扫描字符串 / 字符字面量，quote 为归一化后的引号字符
    Token lexString(char32_t quote);

    // 扫描整数（十进制或 0x 十六进制）
    Token lexNumber();

    // 扫描一个连续的"词"，必要时按最长匹配拆成多个记号
    void lexWordRun(std::vector<Token>& out);
};

}  // namespace zasm

#endif  // ZHASM_LEXER_H