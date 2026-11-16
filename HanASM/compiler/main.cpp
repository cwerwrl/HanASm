// ============================================================================
//  中文汇编器 —— 主流程
// ----------------------------------------------------------------------------
//  流程：读文件 -> Lexer -> Parser -> Pass1 -> Pass2 -> 写 .com -> 打印符号表
//  用法：asm <源文件.asm> [输出.com]
// ============================================================================

#include "common.h"
#include "lexer.h"
#include "parser.h"
#include "pass1.h"
#include "pass2.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

// ----------------------------------------------------------------------------
//  读取整个文件（二进制方式，避免 Windows 下把 \r\n 改写成 \n）
// ----------------------------------------------------------------------------
std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("无法打开源文件：" + path);
    }

    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

// ----------------------------------------------------------------------------
//  写出二进制文件
// ----------------------------------------------------------------------------
void writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("无法创建输出文件：" + path);
    }

    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    if (!out) {
        throw std::runtime_error("写入输出文件失败：" + path);
    }
}

// ----------------------------------------------------------------------------
//  把路径的扩展名替换掉：a.asm -> a.com
// ----------------------------------------------------------------------------
std::string replaceExtension(const std::string& path, const std::string& newExt) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');

    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash)) {
        return path + newExt;
    }
    return path.substr(0, dot) + newExt;
}

// ----------------------------------------------------------------------------
//  打印符号表
// ----------------------------------------------------------------------------
void printSymbolTable(const zasm::Pass1Result& result) {
    std::cout << "\n================ 符 号 表 ================\n";
    std::cout << std::left
              << std::setw(18) << "名称"
              << std::setw(10) << "地址"
              << std::setw(8)  << "字节"
              << "类型\n";
    std::cout << "------------------------------------------\n";

    for (const auto& kv : result.symbols) {
        const zasm::Symbol& sym = kv.second;
        std::cout << std::left
                  << std::setw(18) << sym.name
                  << std::setw(10) << zasm::formatHex(sym.addr)
                  << std::setw(8)  << sym.size
                  << (sym.isVar ? sym.typeName : std::string("标签"))
                  << "\n";
    }

    std::cout << "------------------------------------------\n";

    if (result.hasEntry) {
        std::cout << "入口标签：" << result.entryLabel << "\n";
    }

    std::cout << "结束地址：" << zasm::formatHex(result.endAddr) << "\n";
    std::cout << "代码 + 数据总长：" << (result.endAddr - zasm::START_ADDR) << " 字节\n";
    std::cout << "==========================================\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "中文汇编器 MVP\n"
                  << "用法：asm <源文件.asm> [输出.com]\n";
        return 1;
    }

    const std::string inputPath = argv[1];
    const std::string outputPath =
        (argc >= 3) ? argv[2] : replaceExtension(inputPath, ".com");

    try {
        // 1) 读取源文件
        const std::string source = readFile(inputPath);

        // 2) 词法分析
        zasm::Lexer lexer(source);
        std::vector<zasm::Token> tokens = lexer.run();

        // 3) 语法分析
        zasm::Parser parser(std::move(tokens));
        std::vector<zasm::Instruction> program = parser.parse();

        // 4) Pass 1：符号表与地址计算
        zasm::Pass1Result pass1 = zasm::runPass1(program);

        // 5) Pass 2：机器码生成与回填
        std::vector<std::uint8_t> image = zasm::runPass2(program, pass1.symbols);

        // 6) 写出 .com 文件
        writeFile(outputPath, image);

        std::cout << "编译成功，生成 " << outputPath << "\n";
        std::cout << "共 " << image.size() << " 字节，加载地址 0x100\n";

        // 7) 打印符号表
        printSymbolTable(pass1);
    } catch (const zasm::AsmError& e) {
        std::cerr << "编译失败（第 " << e.line() << " 行）：" << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "错误：" << e.what() << "\n";
        return 1;
    }

    return 0;
}