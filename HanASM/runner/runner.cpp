/**
 * 中文汇编语言运行器 (Runner)
 * 
 * 功能：读取 DOS COM 格式的二进制文件，在 Windows 下原生执行
 * 支持：32位寄存器模拟、中文 UTF-8 输出、系统调用
 * 
 * 编译命令：
 *   g++ -std=c++17 -O2 runner.cpp -o runner.exe
 * 
 * 使用命令：
 *   runner.exe program.com
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include <windows.h>  // Windows API 用于控制台编码设置

// ============================================================================
// 常量定义
// ============================================================================

constexpr uint32_t MEMORY_SIZE = 0x10000;  // 64KB 模拟内存（DOS COM 文件最大 64KB）
constexpr uint32_t BASE_ADDRESS = 0x100;   // COM 文件起始地址

// ============================================================================
// 寄存器枚举与名称映射
// ============================================================================

/// 32 位寄存器枚举（编号严格遵循 x86 编码，中文名只是别名）
/// x86 标准编码：0=EAX 1=ECX 2=EDX 3=EBX 4=ESP 5=EBP 6=ESI 7=EDI
enum class Register32 : uint8_t {
    甲 = 0,  // EAX
    丙 = 1,  // ECX
    丁 = 2,  // EDX
    乙 = 3,  // EBX
    戊 = 4,  // ESP
    己 = 5,  // EBP
    庚 = 6,  // ESI
    辛 = 7   // EDI
};

/// 寄存器名称映射（按 x86 编码索引 0..7 排列，与机器码 rd 字段一一对应）
const char* regNameZh[] = {
    "甲(EAX)",  // 0
    "丙(ECX)",  // 1
    "丁(EDX)",  // 2
    "乙(EBX)",  // 3
    "戊(ESP)",  // 4
    "己(EBP)",  // 5
    "庚(ESI)",  // 6
    "辛(EDI)"   // 7
};

/// 8 位寄存器名称映射（B0+rb 编码：0..7 对应 AL,CL,DL,BL,AH,CH,DH,BH）
const char* reg8NameZh[] = {
    "AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"
};

// ============================================================================
// 标志位定义
// ============================================================================

struct Flags {
    bool ZF;  // 零标志位（Zero Flag）
    
    Flags() : ZF(false) {}
};

// ============================================================================
// 模拟 CPU 类
// ============================================================================

class SimulatedCPU {
private:
    // 32 位寄存器数组
    uint32_t regs[8];
    
    // 模拟内存
    std::vector<uint8_t> memory;
    
    // 标志位
    Flags flags;
    
    // 是否开启调试输出
    bool debugMode;
    
    // 指令指针（程序计数器）
    uint32_t ip;
    
public:
    /// 构造函数
    SimulatedCPU(bool debug = false) 
        : debugMode(debug), ip(BASE_ADDRESS) {
        // 初始化寄存器为 0
        for (int i = 0; i < 8; i++) {
            regs[i] = 0;
        }
        // 初始化内存
        memory.resize(MEMORY_SIZE, 0);
        
        // 设置栈指针（指向内存顶部）
        regs[4] = MEMORY_SIZE - 1;  // ESP
        regs[5] = MEMORY_SIZE - 1;  // EBP
    }
    
    /// 加载 COM 文件到内存
    bool loadComFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "错误：无法打开文件 " << filename << std::endl;
            return false;
        }
        
        // 获取文件大小
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // 检查文件大小是否超过 64KB
        if (size > (MEMORY_SIZE - BASE_ADDRESS)) {
            std::cerr << "错误：文件过大，超过 64KB 限制" << std::endl;
            return false;
        }
        
        // 读取文件内容到内存（从 BASE_ADDRESS 开始）
        file.read(reinterpret_cast<char*>(memory.data() + BASE_ADDRESS), size);
        file.close();
        
        std::cout << "成功加载 " << size << " 字节到内存地址 0x" 
                  << std::hex << BASE_ADDRESS << std::dec << std::endl;
        
        return true;
    }
    
    /// 读取内存字节（带边界检查）
    uint8_t readByte(uint32_t addr) const {
        if (addr >= MEMORY_SIZE) {
            std::cerr << "错误：内存读取越界！地址 0x" << std::hex << addr << std::dec << std::endl;
            exit(1);
        }
        return memory[addr];
    }
    
    /// 读取 32 位值（小端序）
    uint32_t readDWord(uint32_t addr) const {
        if (addr + 3 >= MEMORY_SIZE) {
            std::cerr << "错误：内存读取越界！地址 0x" << std::hex << addr << std::dec << std::endl;
            exit(1);
        }
        return *reinterpret_cast<const uint32_t*>(&memory[addr]);
    }
    
    /// 写入内存字节（带边界检查）
    void writeByte(uint32_t addr, uint8_t value) {
        if (addr >= MEMORY_SIZE) {
            std::cerr << "错误：内存写入越界！地址 0x" << std::hex << addr << std::dec << std::endl;
            exit(1);
        }
        memory[addr] = value;
    }
    
    /// 写入 32 位值（小端序）
    void writeDWord(uint32_t addr, uint32_t value) {
        if (addr + 3 >= MEMORY_SIZE) {
            std::cerr << "错误：内存写入越界！地址 0x" << std::hex << addr << std::dec << std::endl;
            exit(1);
        }
        *reinterpret_cast<uint32_t*>(&memory[addr]) = value;
    }
    
    /// 获取寄存器值
    uint32_t getReg(Register32 reg) const {
        return regs[static_cast<int>(reg)];
    }
    
    /// 设置寄存器值
    void setReg(Register32 reg, uint32_t value) {
        regs[static_cast<int>(reg)] = value;
    }

    /// 设置 8 位寄存器值
    ///
    /// x86 的 8 位寄存器编号与 32 位寄存器共用低 3 位：
    ///   0=AL(EAX低8)  1=CL(ECX低8)  2=DL(EDX低8)  3=BL(EBX低8)
    ///   4=AH(EAX高8)  5=CH(ECX高8)  6=DH(EDX高8)  7=BH(EBX高8)
    /// 即：低两位决定 32 位寄存器(EAX/ECX/EDX/EBX)，第 2 位(值 4)决定高/低字节。
    void setReg8(uint8_t idx, uint8_t value) {
        int regIdx = idx & 0x03;              // 对应的 32 位寄存器：0=EAX,1=ECX,2=EDX,3=EBX
        bool isHigh = (idx & 0x04) != 0;      // 4..7 对应高 8 位（AH/CH/DH/BH）

        if (isHigh) {
            // 替换 bits 8-15，保留低 8 位与高 16 位
            regs[regIdx] = (regs[regIdx] & 0xFFFF00FFu) |
                           (static_cast<uint32_t>(value) << 8);
        } else {
            // 替换 bits 0-7，保留高 24 位
            regs[regIdx] = (regs[regIdx] & 0xFFFFFF00u) | value;
        }
    }

    /// 读取 8 位寄存器值（与 setReg8 的编号规则一致）
    uint8_t getReg8(uint8_t idx) const {
        int regIdx = idx & 0x03;
        bool isHigh = (idx & 0x04) != 0;
        if (isHigh) {
            return static_cast<uint8_t>((regs[regIdx] >> 8) & 0xFF);
        }
        return static_cast<uint8_t>(regs[regIdx] & 0xFF);
    }
    
    /// 获取指令指针
    uint32_t getIP() const {
        return ip;
    }
    
    /// 设置指令指针
    void setIP(uint32_t newIP) {
        ip = newIP;
    }
    
    /// 获取标志位
    const Flags& getFlags() const {
        return flags;
    }
    
    /// 打印当前寄存器状态（调试用）
    void printRegisters() const {
        std::cout << "[调试] IP=0x" << std::hex << ip << std::dec;
        for (int i = 0; i < 8; i++) {
            std::cout << " " << regNameZh[i] << "=0x" << std::hex << regs[i] << std::dec;
        }
        std::cout << " ZF=" << (flags.ZF ? "1" : "0") << std::endl;
    }
    
    /// 解析 ModR/M 字节
    /// 格式：mod(2) reg(3) r/m(3)
    void parseModRM(uint8_t byte, uint8_t& mod, uint8_t& reg, uint8_t& rm) const {
        mod = (byte >> 6) & 0x03;
        reg = (byte >> 3) & 0x07;
        rm = byte & 0x07;
    }
    
    /// 根据 ModR/M 字节计算目标寄存器
    Register32 getRegFromModRM(uint8_t regField) const {
        return static_cast<Register32>(regField);
    }
    
    /// 根据 ModR/M 字节计算内存地址或寄存器
    /// 简化处理：只处理寄存器寻址（mod=11）
    uint32_t computeEffectiveAddress(uint8_t mod, uint8_t rm, int32_t displacement = 0) const {
        if (mod == 0x03) {
            // 寄存器寻址，返回寄存器值
            return regs[rm];  // 这里返回的是寄存器值，不是地址
        }
        
        // 简化处理：只支持寄存器寻址
        std::cerr << "错误：不支持的内存寻址模式 mod=" << (int)mod << std::endl;
        exit(1);
    }
    
    /// 执行一条指令，返回是否继续执行
    bool executeOneInstruction() {
        // 取指
        uint8_t opcode = readByte(ip);
        uint32_t nextIP = ip + 1;
        
        if (debugMode) {
            std::cout << "\n[指令] 0x" << std::hex << ip
                      << " 操作码=0x" << (int)opcode << std::dec;
        }
        
        // 解码并执行
        switch (opcode) {
            case 0xB0: case 0xB1: case 0xB2: case 0xB3:
            case 0xB4: case 0xB5: case 0xB6: case 0xB7: {
                // B0+rb ib: MOV r8, imm8 —— 给 8 位寄存器赋立即数
                // 低 3 位即寄存器编号：
                //   B0=AL  B1=CL  B2=DL  B3=BL  B4=AH  B5=CH  B6=DH  B7=BH
                // 指令总长度 2 字节（操作码 + 1 字节立即数）
                uint8_t regIndex = opcode & 0x07;
                uint8_t value = readByte(nextIP);
                nextIP += 1;
                setReg8(regIndex, value);

                if (debugMode) {
                    std::cout << " - 装8位 " << reg8NameZh[regIndex]
                              << ", 0x" << std::hex << (int)value << std::dec;
                }
                break;
            }

            case 0xB8: case 0xB9: case 0xBA: case 0xBB:
            case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
                // B8+rd id: MOV r32, imm32
                // 操作码低 3 位表示寄存器编号
                uint8_t regIndex = opcode & 0x07;
                Register32 reg = static_cast<Register32>(regIndex);
                
                uint32_t value = readDWord(nextIP);
                setReg(reg, value);
                nextIP += 4;
                
                if (debugMode) {
                    std::cout << " - 装 " << regNameZh[regIndex] 
                              << ", 0x" << std::hex << value << std::dec;
                }
                break;
            }
            
            case 0x89: {
                // 89 /r: MOV r/m32, r32
                uint8_t modrm = readByte(nextIP++);
                uint8_t mod, reg, rm;
                parseModRM(modrm, mod, reg, rm);
                
                if (mod == 0x03) {
                    // 寄存器到寄存器
                    Register32 src = getRegFromModRM(reg);
                    Register32 dst = getRegFromModRM(rm);
                    setReg(dst, getReg(src));
                    
                    if (debugMode) {
                        std::cout << " - 传 " << regNameZh[static_cast<int>(src)]
                                  << " -> " << regNameZh[static_cast<int>(dst)];
                    }
                } else {
                    std::cerr << "错误：不支持的内存寻址" << std::endl;
                    return false;
                }
                break;
            }
            
            case 0x03: {
                // 03 /r: ADD r32, r/m32
                uint8_t modrm = readByte(nextIP++);
                uint8_t mod, reg, rm;
                parseModRM(modrm, mod, reg, rm);
                
                if (mod == 0x03) {
                    Register32 src = getRegFromModRM(reg);
                    Register32 dst = getRegFromModRM(rm);
                    
                    uint32_t result = getReg(dst) + getReg(src);
                    setReg(dst, result);
                    
                    if (debugMode) {
                        std::cout << " - 加 " << regNameZh[static_cast<int>(dst)]
                                  << " += " << regNameZh[static_cast<int>(src)];
                    }
                } else {
                    std::cerr << "错误：不支持的内存寻址" << std::endl;
                    return false;
                }
                break;
            }
            
            case 0x2B: {
                // 2B /r: SUB r32, r/m32
                uint8_t modrm = readByte(nextIP++);
                uint8_t mod, reg, rm;
                parseModRM(modrm, mod, reg, rm);
                
                if (mod == 0x03) {
                    Register32 src = getRegFromModRM(reg);
                    Register32 dst = getRegFromModRM(rm);
                    
                    uint32_t result = getReg(dst) - getReg(src);
                    setReg(dst, result);
                    
                    if (debugMode) {
                        std::cout << " - 减 " << regNameZh[static_cast<int>(dst)]
                                  << " -= " << regNameZh[static_cast<int>(src)];
                    }
                } else {
                    std::cerr << "错误：不支持的内存寻址" << std::endl;
                    return false;
                }
                break;
            }
            
            case 0x3B: {
                // 3B /r: CMP r32, r/m32
                uint8_t modrm = readByte(nextIP++);
                uint8_t mod, reg, rm;
                parseModRM(modrm, mod, reg, rm);
                
                if (mod == 0x03) {
                    Register32 src = getRegFromModRM(reg);
                    Register32 dst = getRegFromModRM(rm);
                    
                    uint32_t val1 = getReg(dst);
                    uint32_t val2 = getReg(src);
                    
                    // 设置零标志位
                    flags.ZF = (val1 == val2);
                    
                    if (debugMode) {
                        std::cout << " - 比较 " << regNameZh[static_cast<int>(dst)]
                                  << " 与 " << regNameZh[static_cast<int>(src)]
                                  << " -> ZF=" << (flags.ZF ? "1" : "0");
                    }
                } else {
                    std::cerr << "错误：不支持的内存寻址" << std::endl;
                    return false;
                }
                break;
            }
            
            case 0x40: case 0x41: case 0x42: case 0x43:
            case 0x44: case 0x45: case 0x46: case 0x47: {
                // 40+rd: INC r32
                uint8_t regIndex = opcode & 0x07;
                Register32 reg = static_cast<Register32>(regIndex);
                
                uint32_t value = getReg(reg) + 1;
                setReg(reg, value);
                
                if (debugMode) {
                    std::cout << " - 自增 " << regNameZh[regIndex];
                }
                break;
            }
            
            case 0xE9: {
                // E9 rel32: JMP 相对跳转
                int32_t offset = static_cast<int32_t>(readDWord(nextIP));
                nextIP += 4;
                
                // 计算目标地址：当前指令地址 + 5（指令长度）+ 偏移量
                uint32_t target = ip + 5 + offset;
                setIP(target);
                
                if (debugMode) {
                    std::cout << " - 无条件跳转 0x" << std::hex << target << std::dec;
                }
                
                // 跳转后直接继续，不更新 nextIP
                return true;
            }
            
            case 0x0F: {
                // 两字节指令前缀
                uint8_t subOpcode = readByte(nextIP++);
                
                switch (subOpcode) {
                    case 0x84: {
                        // 0F 84 rel32: JE（等于跳转）
                        int32_t offset = static_cast<int32_t>(readDWord(nextIP));
                        nextIP += 4;
                        
                        if (flags.ZF) {
                            uint32_t target = ip + 6 + offset;
                            setIP(target);
                            if (debugMode) {
                                std::cout << " - 等跳 (ZF=1) -> 0x" << std::hex << target << std::dec;
                            }
                            return true;
                        }
                        
                        if (debugMode) {
                            std::cout << " - 等跳 (ZF=0) 不跳";
                        }
                        break;
                    }
                    
                    case 0x85: {
                        // 0F 85 rel32: JNE（不等跳转）
                        int32_t offset = static_cast<int32_t>(readDWord(nextIP));
                        nextIP += 4;
                        
                        if (!flags.ZF) {
                            uint32_t target = ip + 6 + offset;
                            setIP(target);
                            if (debugMode) {
                                std::cout << " - 不等跳 (ZF=0) -> 0x" << std::hex << target << std::dec;
                            }
                            return true;
                        }
                        
                        if (debugMode) {
                            std::cout << " - 不等跳 (ZF=1) 不跳";
                        }
                        break;
                    }
                    
                    default:
                        std::cerr << "错误：未知的两字节指令 0F " << std::hex << (int)subOpcode << std::dec << std::endl;
                        return false;
                }
                break;
            }
            
            case 0xCD: {
                // CD 21: INT 21h 系统调用
                uint8_t intNum = readByte(nextIP++);
                
                if (intNum == 0x21) {
                    // 执行系统调用
                    if (!handleSyscall()) {
                        return false;  // 程序结束
                    }
                } else {
                    std::cerr << "错误：未知的中断 INT " << std::hex << (int)intNum << std::dec << std::endl;
                    return false;
                }
                break;
            }
            
            default:
                std::cerr << "错误：未知指令 0x" << std::hex << (int)opcode << std::dec 
                          << " 在地址 0x" << std::hex << ip << std::dec << std::endl;
                return false;
        }
        
        // 更新指令指针
        setIP(nextIP);
        
        // 调试输出
        if (debugMode) {
            std::cout << std::endl;
            printRegisters();
        }
        
        return true;
    }
    
private:
    /// 处理系统调用（INT 21h）
    /// 返回 true 表示继续执行，false 表示程序结束
    bool handleSyscall() {
        // 获取 AH 寄存器：AH 属于 EAX(甲)，对应 8 位寄存器编号 4
        // （B4 09 写入的正是编号 4 的 AH，这里用同一套 getReg8 规则读取，保证一致）
        uint8_t ah = getReg8(4);
        
        switch (ah) {
            case 0x09: {
                // AH=09: 打印字符串
                // EDX 指向字符串地址，$ 结束符
                uint32_t strAddr = regs[3];  // EDX
                
                // 读取字符串直到遇到 $
                std::string output;
                while (true) {
                    uint8_t ch = readByte(strAddr);
                    if (ch == 0x24) {  // $ 结束符
                        break;
                    }
                    output += static_cast<char>(ch);
                    strAddr++;
                }
                
                // 输出字符串（支持 UTF-8）
                std::cout << output;
                std::cout.flush();
                
                if (debugMode) {
                    std::cout << " [系统调用: 打印字符串]" << std::endl;
                }
                break;
            }
            
            case 0x4C: {
                // AH=4C: 程序退出
                // AL 中存放退出码（AL 属于 EAX(甲)，对应 8 位寄存器编号 0）
                uint8_t exitCode = getReg8(0);
                
                if (debugMode) {
                    std::cout << "\n[系统调用: 程序退出，退出码=" << (int)exitCode << "]" << std::endl;
                }
                
                return false;  // 停止执行
            }
            
            default:
                std::cerr << "错误：未知的系统调用 AH=0x" << std::hex << (int)ah << std::dec << std::endl;
                return false;
        }
        
        return true;
    }
};

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char* argv[]) {
    // 设置 Windows 控制台为 UTF-8 编码，支持中文输出
    SetConsoleOutputCP(CP_UTF8);
    
    std::cout << "========================================" << std::endl;
    std::cout << "     中文汇编语言运行器 v1.0" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 检查命令行参数
    if (argc < 2) {
        std::cout << "用法: " << argv[0] << " <程序.com> [选项]" << std::endl;
        std::cout << std::endl;
        std::cout << "选项:" << std::endl;
        std::cout << "  -d    开启调试模式（每条指令后打印寄存器状态）" << std::endl;
        std::cout << std::endl;
        std::cout << "示例:" << std::endl;
        std::cout << "  " << argv[0] << " hello.com" << std::endl;
        std::cout << "  " << argv[0] << " test.com -d" << std::endl;
        return 1;
    }
    
    // 解析命令行参数
    std::string filename = argv[1];
    bool debugMode = false;
    
    if (argc >= 3) {
        std::string arg = argv[2];
        if (arg == "-d" || arg == "--debug") {
            debugMode = true;
        }
    }
    
    // 创建 CPU 实例
    SimulatedCPU cpu(debugMode);
    
    // 加载 COM 文件
    if (!cpu.loadComFile(filename)) {
        return 1;
    }
    
    std::cout << std::endl << "开始执行..." << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    // 主执行循环
    int instructionCount = 0;
    while (true) {
        // 执行一条指令
        if (!cpu.executeOneInstruction()) {
            break;
        }
        
        instructionCount++;
        
        // 安全限制：防止无限循环
        if (instructionCount > 1000000) {
            std::cerr << "\n错误：执行超过 100 万条指令，可能陷入无限循环" << std::endl;
            break;
        }
    }
    
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "程序执行完成，共执行 " << instructionCount << " 条指令" << std::endl;
    
    return 0;
}
