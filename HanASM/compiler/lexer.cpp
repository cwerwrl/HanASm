// ============================================================================
//  词法分析器实现
// ============================================================================

#include "lexer.h"

#include <unordered_map>

namespace zasm {

namespace {

// ----------------------------------------------------------------------------
//  关键词条目：类型 + 规范化后的文本
//
//  规范化是"翻译"的核心：中文词与英文词最终都变成同一套英文助记符，
//  Parser 与后续 Pass 只需面对 MOV / ADD / EAX / INT 这类统一名字。
// ----------------------------------------------------------------------------
struct KeywordInfo {
    TokenType type;
    std::string canon;
};

// 把 ASCII 字母转成小写（中文字符不受影响），用于大小写不敏感匹配
std::u32string lowerAscii(std::u32string text) {
    for (char32_t& c : text) {
        if (c >= U'A' && c <= U'Z') {
            c = static_cast<char32_t>(c - U'A' + U'a');
        }
    }
    return text;
}

bool isDigitChar(char32_t c) {
    return c >= U'0' && c <= U'9';
}

bool isHexDigitChar(char32_t c) {
    return isDigitChar(c) ||
           (c >= U'a' && c <= U'f') ||
           (c >= U'A' && c <= U'F');
}

// ----------------------------------------------------------------------------
//  关键词表
//
//  * 中文键按"最长匹配"使用，因此同时存在 "加" 与 "加即"、"跳" 与 "不等跳"
//    时，会优先命中更长的词；
//  * 英文键统一用小写，查表前会把候选词转小写，实现大小写不敏感；
//  * "int" 被保留为数据类型（整数/int），中断指令请使用中文 "系调" 或
//    英文 "syscall"，避免与类型名冲突。
// ----------------------------------------------------------------------------
const std::unordered_map<std::u32string, KeywordInfo>& keywordTable() {
    static const std::unordered_map<std::u32string, KeywordInfo> table = {
        // ---------------- 指令：传送 ----------------
        {U"装",     {TokenType::INSTRUCTION, "MOV"}},
        {U"读双",   {TokenType::INSTRUCTION, "MOV"}},   // 读双字 = 取数
        {U"写双",   {TokenType::INSTRUCTION, "MOV"}},   // 写双字 = 存数

        // ---------------- 指令：算术 / 逻辑 ----------------
        {U"加",     {TokenType::INSTRUCTION, "ADD"}},
        {U"加即",   {TokenType::INSTRUCTION, "ADD"}},   // 加立即数
        {U"减",     {TokenType::INSTRUCTION, "SUB"}},
        {U"比",     {TokenType::INSTRUCTION, "CMP"}},
        {U"自增",   {TokenType::INSTRUCTION, "INC"}},

        // ---------------- 指令：跳转 ----------------
        {U"跳",     {TokenType::INSTRUCTION, "JMP"}},
        {U"调",     {TokenType::INSTRUCTION, "CALL"}},
        {U"等跳",   {TokenType::INSTRUCTION, "JE"}},
        {U"不等跳", {TokenType::INSTRUCTION, "JNE"}},

        // ---------------- 指令：栈 ----------------
        {U"压",     {TokenType::INSTRUCTION, "PUSH"}},
        {U"弹",     {TokenType::INSTRUCTION, "POP"}},

        // ---------------- 指令：其他 ----------------
        {U"停",     {TokenType::INSTRUCTION, "HLT"}},
        {U"空",     {TokenType::INSTRUCTION, "NOP"}},
        {U"返",     {TokenType::INSTRUCTION, "RET"}},
        {U"系调",   {TokenType::INSTRUCTION, "INT"}},   // 系统调用 = INT 21H

        // ---------------- 寄存器 ----------------
        // 编号与 x86 编码顺序一致：0=EAX(甲) 1=ECX(乙) 2=EDX(丙) 3=EBX(丁)
        {U"甲",     {TokenType::REGISTER, "EAX"}},
        {U"乙",     {TokenType::REGISTER, "ECX"}},
        {U"丙",     {TokenType::REGISTER, "EDX"}},
        {U"丁",     {TokenType::REGISTER, "EBX"}},

        // ---------------- 数据类型 ----------------
        {U"整数",   {TokenType::TYPE, "INT"}},
        {U"字符",   {TokenType::TYPE, "CHAR"}},
        {U"字串",   {TokenType::TYPE, "STRING"}},

        // ---------------- 伪指令 ----------------
        {U"入口",   {TokenType::DIRECTIVE, "#ENTRY"}},
        {U"段",     {TokenType::DIRECTIVE, "#SEG"}},

        // ---------------- 英文指令 ----------------
        {U"mov",    {TokenType::INSTRUCTION, "MOV"}},
        {U"add",    {TokenType::INSTRUCTION, "ADD"}},
        {U"sub",    {TokenType::INSTRUCTION, "SUB"}},
        {U"cmp",    {TokenType::INSTRUCTION, "CMP"}},
        {U"inc",    {TokenType::INSTRUCTION, "INC"}},
        {U"jmp",    {TokenType::INSTRUCTION, "JMP"}},
        {U"call",   {TokenType::INSTRUCTION, "CALL"}},
        {U"je",     {TokenType::INSTRUCTION, "JE"}},
        {U"jne",    {TokenType::INSTRUCTION, "JNE"}},
        {U"push",   {TokenType::INSTRUCTION, "PUSH"}},
        {U"pop",    {TokenType::INSTRUCTION, "POP"}},
        {U"hlt",    {TokenType::INSTRUCTION, "HLT"}},
        {U"nop",    {TokenType::INSTRUCTION, "NOP"}},
        {U"ret",    {TokenType::INSTRUCTION, "RET"}},
        {U"syscall",{TokenType::INSTRUCTION, "INT"}},

        // ---------------- 英文寄存器 ----------------
        {U"eax",    {TokenType::REGISTER, "EAX"}},
        {U"ecx",    {TokenType::REGISTER, "ECX"}},
        {U"edx",    {TokenType::REGISTER, "EDX"}},
        {U"ebx",    {TokenType::REGISTER, "EBX"}},

        // ---------------- 英文类型 ----------------
        {U"int",    {TokenType::TYPE, "INT"}},
        {U"char",   {TokenType::TYPE, "CHAR"}},
        {U"string", {TokenType::TYPE, "STRING"}},

        // ---------------- 英文伪指令 ----------------
        {U"entry",  {TokenType::DIRECTIVE, "#ENTRY"}},
        {U"segment",{TokenType::DIRECTIVE, "#SEG"}},
        {U"seg",    {TokenType::DIRECTIVE, "#SEG"}},
    };
    return table;
}

}  // namespace

// ----------------------------------------------------------------------------
//  构造：处理 BOM 并整体解码为码点
// ----------------------------------------------------------------------------
Lexer::Lexer(std::string source) {
    // UTF-8 BOM（EF BB BF）会被很多编辑器自动加上，这里直接跳过
    if (source.size() >= 3 &&
        static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB &&
        static_cast<unsigned char>(source[2]) == 0xBF) {
        source.erase(0, 3);
    }
    src_ = utf8Decode(source);
}

char32_t Lexer::peekRaw(std::size_t offset) const {
    if (pos_ + offset >= src_.size()) return U'\0';
    return src_[pos_ + offset];
}

char32_t Lexer::peekNorm(std::size_t offset) const {
    return normalizePunct(peekRaw(offset));
}

// ----------------------------------------------------------------------------
//  跳过空白（含全角空格）与四种注释
// ----------------------------------------------------------------------------
void Lexer::skipTrivia() {
    while (!atEnd()) {
        char32_t raw = src_[pos_];
        char32_t c = normalizePunct(raw);

        if (c == U' ' || c == U'\t' || c == U'\r') {
            ++pos_;
            continue;
        }

        // 注释一：分号 ; 到行尾      注释二：汉字 "注" 到行尾
        if (c == U';' || raw == U'注') {
            while (!atEnd() && src_[pos_] != U'\n') ++pos_;
            continue;
        }

        // 注释三：// 到行尾
        if (c == U'/' && peekNorm(1) == U'/') {
            pos_ += 2;
            while (!atEnd() && src_[pos_] != U'\n') ++pos_;
            continue;
        }

        // 注释四：/* ... */（可跨行，内部换行需要同步行号）
        if (c == U'/' && peekNorm(1) == U'*') {
            int startLine = line_;
            pos_ += 2;
            bool closed = false;
            while (!atEnd()) {
                if (src_[pos_] == U'\n') {
                    ++line_;
                    ++pos_;
                    continue;
                }
                if (src_[pos_] == U'*' && peekNorm(1) == U'/') {
                    pos_ += 2;
                    closed = true;
                    break;
                }
                ++pos_;
            }
            if (!closed) {
                throw AsmError("块注释没有闭合（缺少 */）", startLine);
            }
            continue;
        }

        break;
    }
}

// ----------------------------------------------------------------------------
//  字符串 / 字符字面量
//  字符串内部的标点一律原样保留，不做全角转半角。
// ----------------------------------------------------------------------------
Token Lexer::lexString(char32_t quote) {
    const int startLine = line_;
    const char32_t rawStart = src_[pos_];
    // 如果起始引号是全角的（“ ” ‘ ’），则结束符允许匹配同向的全角引号
    const bool fullWidthStart = (rawStart != quote);

    ++pos_;   // 跳过起始引号

    std::u32string value;
    bool closed = false;

    while (!atEnd()) {
        char32_t raw = src_[pos_];

        if (raw == U'\n') break;   // 字符串不允许跨行

        if (raw == U'\\') {        // 反斜杠转义
            ++pos_;
            if (atEnd()) break;
            char32_t esc = src_[pos_++];
            switch (esc) {
                case U'n':  value.push_back(U'\n'); break;
                case U't':  value.push_back(U'\t'); break;
                case U'r':  value.push_back(U'\r'); break;
                case U'0':  value.push_back(U'\0'); break;
                case U'\\': value.push_back(U'\\'); break;
                case U'"':  value.push_back(U'"');  break;
                case U'\'': value.push_back(U'\''); break;
                default:    value.push_back(esc);   break;
            }
            continue;
        }

        if (raw == quote || (fullWidthStart && normalizePunct(raw) == quote)) {
            ++pos_;
            closed = true;
            break;
        }

        value.push_back(raw);
        ++pos_;
    }

    if (!closed) {
        throw AsmError("字符串没有闭合（缺少末尾引号）", startLine);
    }

    if (quote == U'\'') {
        if (value.size() != 1) {
            throw AsmError("字符字面量必须恰好包含一个字符", startLine);
        }
        // CHAR_LITERAL 的 text 直接存十进制码值，后续 Pass 用 parseIntLiteral 读取
        return makeToken(TokenType::CHAR_LITERAL,
                         std::to_string(static_cast<int>(value[0])),
                         startLine);
    }

    return makeToken(TokenType::STRING_LITERAL, utf8Encode(value), startLine);
}

// ----------------------------------------------------------------------------
//  整数：十进制或 0x 十六进制
// ----------------------------------------------------------------------------
Token Lexer::lexNumber() {
    const int startLine = line_;
    std::u32string buffer;

    // 形式一：汇编风格的后缀写法，如 4Ch、0FFH
    {
        std::size_t scan = pos_;
        while (scan < src_.size() && isHexDigitChar(normalizePunct(src_[scan]))) {
            ++scan;
        }
        if (scan > pos_ && scan < src_.size()) {
            const char32_t suffix = normalizePunct(src_[scan]);
            if (suffix == U'h' || suffix == U'H') {
                buffer.assign(src_.begin() + static_cast<std::ptrdiff_t>(pos_),
                              src_.begin() + static_cast<std::ptrdiff_t>(scan + 1));
                pos_ = scan + 1;
                return makeToken(TokenType::INT_LITERAL, utf8Encode(buffer), startLine);
            }
        }
    }

    // 形式二：0x 前缀的十六进制
    if (peekNorm() == U'0' && (peekNorm(1) == U'x' || peekNorm(1) == U'X')) {
        buffer.push_back(src_[pos_++]);   // 0
        buffer.push_back(src_[pos_++]);   // x
        while (!atEnd() && isHexDigitChar(normalizePunct(src_[pos_]))) {
            buffer.push_back(src_[pos_++]);
        }
    } else {
        // 形式三：十进制
        while (!atEnd() && isDigitChar(normalizePunct(src_[pos_]))) {
            buffer.push_back(src_[pos_++]);
        }
    }

    return makeToken(TokenType::INT_LITERAL, utf8Encode(buffer), startLine);
}

// ----------------------------------------------------------------------------
//  词元扫描 + 最长匹配
//
//  例：
//    "装 甲, 10"           -> INSTRUCTION(MOV) REGISTER(EAX) PUNCT(,) INT(10)
//    "不等跳 结束"          -> INSTRUCTION(JNE) IDENTIFIER(结束)
//    "装甲, 10"（无空格）    -> 依次匹配 "装"、"甲"，不会被当成一个词
//    "计数器"（变量名）      -> 没有任何前缀命中，整体作为 IDENTIFIER
// ----------------------------------------------------------------------------
void Lexer::lexWordRun(std::vector<Token>& out) {
    std::u32string run;
    while (!atEnd() && isWordChar(src_[pos_])) {
        run.push_back(src_[pos_]);
        ++pos_;
    }

    bool asciiOnly = true;
    for (char32_t c : run) {
        if (c > 0x7F) {
            asciiOnly = false;
            break;
        }
    }

    const auto& table = keywordTable();

    // 纯 ASCII 的词先整体查表：
    // 这样 "loop1"、"counter"、"inc_flag" 之类标识符不会被误拆成关键词 + 残留。
    if (asciiOnly) {
        auto it = table.find(lowerAscii(run));
        if (it != table.end()) {
            out.push_back(makeToken(it->second.type, it->second.canon, line_));
        } else {
            out.push_back(makeToken(TokenType::IDENTIFIER, utf8Encode(run), line_));
        }
        return;
    }

    // 含中文：从当前下标开始做"最长前缀匹配"
    std::size_t i = 0;
    while (i < run.size()) {
        std::size_t matchedLen = 0;
        const KeywordInfo* matched = nullptr;

        // 长度从剩余部分的最大值开始递减，第一个命中的就是最长匹配
        for (std::size_t len = run.size() - i; len >= 1; --len) {
            auto it = table.find(lowerAscii(run.substr(i, len)));
            if (it != table.end()) {
                matchedLen = len;
                matched = &it->second;
                break;
            }
        }

        if (matched != nullptr) {
            out.push_back(makeToken(matched->type, matched->canon, line_));
            i += matchedLen;
        } else {
            // 一个关键词都匹配不上，剩余部分整体当作标识符（如中文变量名）
            out.push_back(makeToken(TokenType::IDENTIFIER, utf8Encode(run.substr(i)), line_));
            break;
        }
    }
}

// ----------------------------------------------------------------------------
//  主循环
// ----------------------------------------------------------------------------
std::vector<Token> Lexer::run() {
    std::vector<Token> tokens;

    while (true) {
        skipTrivia();

        if (atEnd()) {
            tokens.push_back(makeToken(TokenType::END, "", line_));
            break;
        }

        const char32_t raw = src_[pos_];
        const char32_t c = normalizePunct(raw);

        if (c == U'\n') {
            tokens.push_back(makeToken(TokenType::NEWLINE, "\n", line_));
            ++pos_;
            ++line_;
            continue;
        }

        if (c == U'"' || c == U'\'') {
            tokens.push_back(lexString(c));
            continue;
        }

        if (isDigitChar(c)) {
            tokens.push_back(lexNumber());
            continue;
        }

        if (isWordChar(raw)) {
            lexWordRun(tokens);
            continue;
        }

        if (isPunctChar(c)) {
            tokens.push_back(makeToken(TokenType::PUNCT,
                                       utf8Encode(std::u32string(1, c)),
                                       line_));
            ++pos_;
            continue;
        }

        throw AsmError("无法识别的字符：'" + utf8Encode(std::u32string(1, raw)) + "'", line_);
    }

    return tokens;
}

}  // namespace zasm