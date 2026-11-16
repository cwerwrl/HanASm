# 中文汇编器 HanASM · VS Code 插件

为中文汇编语言 **HanASM** 提供：

- **语法高亮**：指令（装、加、跳、系统调用……）、寄存器（甲、乙、栈顶……）、类型（整数、字符、字串）、标签、数字、字符串、三种注释（`;`、`//`、`注`）。
- **一键编译**：命令 `HanASM: 编译当前文件`（`han-asm.build`），快捷键 `Ctrl+Alt+B`，编辑器右键菜单。
- **代码片段**：`main`、`mov`、`add`、`cmp`、`jmp`、`print`、`data`、`seg`、`loop`。

---

## 1. 目录结构

```
han-asm-vscode/
├─ .vscode/
│  ├─ launch.json              # F5 调试配置
│  └─ tasks.json               # tsc watch 构建任务
├─ examples/
│  └─ 示例.hasm                # 用于验证高亮 / 编译的示例
├─ snippets/
│  └─ han-asm.json             # 代码片段
├─ syntaxes/
│  └─ han-asm.tmLanguage.json  # TextMate 语法高亮
├─ src/
│  └─ extension.ts             # 插件核心逻辑：调用 asm.exe
├─ language-configuration.json # 注释符号、括号匹配、自动闭合
├─ package.json                # 插件清单：语言、命令、菜单、快捷键、设置
├─ tsconfig.json
└─ README.md
```

---

## 2. 从空文件夹开始（完整步骤）

### 2.1 准备环境

1. 安装 **Node.js 18 或更高版本**（推荐 LTS）：<https://nodejs.org/>
2. 安装 **VS Code**：<https://code.visualstudio.com/>
3. 打开 PowerShell，确认环境：

```powershell
node -v
npm -v
```

### 2.2 新建插件目录

```powershell
mkdir D:\han-asm-vscode
cd D:\han-asm-vscode
```

### 2.3 放入本项目文件

把本仓库中的以下文件/文件夹原样复制到 `D:\han-asm-vscode`：

```
package.json
tsconfig.json
language-configuration.json
syntaxes\
snippets\
src\
examples\
.vscode\
```

也可以先只复制代码文件，`npm install` 之后这些文件会自动生成依赖目录。

### 2.4 安装开发依赖

`package.json` 已经声明了三项开发依赖：

| 依赖 | 用途 |
| --- | --- |
| `typescript` | 把 `src/extension.ts` 编译成 `out/extension.js` |
| `@types/vscode` | VS Code 扩展 API 的类型定义 |
| `@types/node` | `child_process`、`fs`、`path`、`util` 等 Node API 的类型 |

在项目根目录执行：

```powershell
npm install
```

如果默认源太慢，可换国内镜像：

```powershell
npm install --registry=https://registry.npmmirror.com
```

> 注意：`@types/vscode` 的版本必须 **不大于** `package.json` 中 `engines.vscode` 声明的版本，否则会出现 “types are for a newer version of VS Code” 之类的提示。本项目两者都是 `^1.75.0`。

### 2.5 编译 TypeScript

```powershell
npm run compile
```

成功后会出现 `out\extension.js` 和 `out\extension.js.map`。

也可以让编译器持续监听（F5 调试时会自动启动）：

```powershell
npm run watch
```

---

## 3. 在本地按 F5 调试插件

1. 用 VS Code **打开文件夹** `D:\han-asm-vscode`（必须是文件夹根目录，不能只打开单个文件）。
2. 按 **F5**，或点击左侧“运行和调试”面板顶部的绿色三角。
3. 首次运行会自动执行 `.vscode/tasks.json` 里的 `npm: watch` 任务，然后弹出一个新窗口：
   **`[扩展开发宿主]`**。
4. 在新窗口中打开 `examples\示例.hasm`。
   - 右下角语言模式应显示 **“中文汇编”**。
   - 指令、寄存器、类型、标签、数字、注释应呈现不同颜色。
5. 按 `Ctrl+Alt+B`，或在编辑器右键选择 **“HanASM: 编译当前文件”**。
   - 编译结果输出到 **“输出”面板 → HanASM 频道**。
   - 若把设置 `hanAsm.output` 改为 `terminal`，则直接在集成终端中运行 `asm.exe`。

停止调试：关闭 `[扩展开发宿主]` 窗口即可。

---

## 4. 三个功能的验证方法

### 4.1 语法高亮

在 `examples\示例.hasm` 中确认这些 token 的颜色差异：

| 内容 | TextMate scope |
| --- | --- |
| `装` `加` `跳` `系统调用` | `keyword.control.instruction.han-asm` |
| `甲` `乙` `栈顶` | `variable.other.register.han-asm` |
| `整数` `字符` `字串` | `storage.type.han-asm` |
| `入口:` `求和:` | `entity.name.tag.han-asm` |
| `0x1F` `42` `3.14` | `constant.numeric.han-asm` |
| `"你好\n"` | `string.quoted.double.han-asm` |
| `; 注释`、`// 注释`、`注 注释` | `comment.line.*.han-asm` |

精确定位某个 token 的 scope：在开发宿主窗口按 `Ctrl+Shift+P`，执行
`Developer: Inspect Editor Tokens and Scopes`，再点击对应文字。

### 4.2 代码片段

新建一个 `.hasm` 文件，输入：

| 输入 | 按 `Tab` 后 |
| --- | --- |
| `main` | `入口:`、缩进空行、`停` |
| `mov` | `装 甲, 0`（光标停在 `甲`） |
| `cmp` | `比较 甲, 乙` + `跳等 标签` |
| `seg` | 数据段 / 代码段的完整骨架 |
| `loop` | 计数循环 |

中文前缀同样可用，例如输入 `装`、`比较`、`入口`。

### 4.3 一键编译

1. 把 `asm.exe` 放到 **源文件同目录**（例如 `examples\asm.exe`），或放到工作区根目录。
2. 打开 `.hasm` 文件，按 `Ctrl+Alt+B`。
3. 输出示例：

```
[10:24:31] ▶ 编译 示例.hasm
    工作目录：D:\han-asm-vscode\examples
    命令："D:\han-asm-vscode\examples\asm.exe" "D:\han-asm-vscode\examples\示例.hasm"
...
[10:24:31] ✔ 编译成功（86 ms）
```

也可以右键 `.hasm` 文件 → **HanASM: 编译当前文件**；资源管理器里的 `.hasm` 文件同样支持右键。

---

## 5. asm.exe 相关设置

插件对所有可配置项都提供了默认值，通常只需处理 `asm.exe` 的位置和参数。

### 5.1 编译器查找顺序

未设置 `hanAsm.executablePath` 时，依次查找：

1. 源文件同目录 `asm.exe`（Windows）或 `asm`（Linux/macOS）
2. 源文件目录 `bin\asm.exe`
3. 工作区根目录 `asm.exe`、`bin\asm.exe`
4. 系统 `PATH`

也可以显式指定：

```json
{
  "hanAsm.executablePath": "D:\\han-asm-vscode\\tools\\asm.exe"
}
```

支持变量：`${workspaceFolder}`、`${fileDirname}`。例如：

```json
{
  "hanAsm.executablePath": "${workspaceFolder}\\tools\\asm.exe"
}
```

### 5.2 编译参数

默认只把源文件的**绝对路径**传给 `asm.exe`：

```json
{
  "hanAsm.arguments": ["${file}"]
}
```

如果 `asm.exe` 需要指定输出文件，请按它的真实用法修改，例如：

```json
{
  "hanAsm.arguments": [
    "${file}",
    "-o",
    "${fileDirname}\\${fileBasenameNoExtension}.bin"
  ]
}
```

可用变量：

| 变量 | 含义 |
| --- | --- |
| `${file}` | 当前文件绝对路径 |
| `${fileDirname}` | 当前文件所在目录 |
| `${fileBasename}` | 文件名（含扩展名） |
| `${fileBasenameNoExtension}` | 文件名（不含扩展名） |
| `${fileExtname}` | 扩展名 |
| `${workspaceFolder}` | 工作区根目录 |

### 5.3 输出乱码

`asm.exe` 若在中文 Windows 控制台输出 GBK，而插件按 UTF-8 解码，会出现乱码。默认 `hanAsm.encoding = "auto"` 会先尝试 UTF-8，失败后回退 GBK。如仍有问题，可显式指定：

```json
{
  "hanAsm.encoding": "gbk"
}
```

### 5.4 输出位置

```json
{
  "hanAsm.output": "outputChannel"
}
```

- `outputChannel`（默认）：输出写入 **“输出”面板 → HanASM**，插件可以拿到退出码并判断成功/失败。
- `terminal`：直接在集成终端里以 `asm.exe` 作为 shell 启动，输出最接近手工运行，但插件无法获知退出码。

---

## 6. 文件扩展名与语言关联

插件默认关联：

- `.hasm`
- `.hanasm`

这样不会抢占已有的 x86 `.asm` 文件。若你的中文汇编源码就是 `.asm`，在 **工作区** `.vscode\settings.json` 中加：

```json
{
  "files.associations": {
    "*.asm": "han-asm"
  }
}
```

或者点击右下角语言模式 → **“为 *.asm 配置文件关联”** → 选择“中文汇编”。

---

## 7. 打包成 vsix（可选）

```powershell
npm install -g @vscode/vsce
vsce package
```

生成 `han-asm-0.1.0.vsix` 后：

- VS Code 扩展面板右上角 `...` → **从 VSIX 安装**
- 或命令行：`code --install-extension han-asm-0.1.0.vsix`

发布到市场前，需要把 `package.json` 的 `publisher` 改成你在 Marketplace 注册的 publisher ID，并补上 `repository` 字段。

---

## 8. 常见问题

**F5 没有反应 / 提示 “preLaunchTask 找不到”**

- 确认 VS Code 打开的是**文件夹根目录**，且存在 `.vscode\tasks.json`。
- 确认已执行 `npm install`，`node_modules\.bin\tsc.cmd` 存在。
- 也可以在“终端 → 运行任务”中手动执行 `npm: watch`，再按 F5。

**改了 `extension.ts` 但新窗口没变化**

- `npm: watch` 负责增量编译；如果 watch 任务已停止，重新执行 `npm run compile` 或重启调试。
- 关闭旧的扩展开发宿主窗口，重新 F5。

**高亮不对 / 全是纯文本**

- 右下角语言模式必须是“中文汇编”（`han-asm`）。
- 检查 `syntaxes\han-asm.tmLanguage.json` 是否有 JSON 语法错误（VS Code 输出面板会提示 “Unable to load grammar”）。
- 指令与操作数之间需要空格，例如 `装 甲, 1`；`装备` 这类词不会被误判为 `装`。

**提示 “未找到 asm.exe”**

- 把 `asm.exe` 放到源文件同目录，或设置 `hanAsm.executablePath`。
- 在设置中搜索 `HanASM`，可直接编辑所有相关项。

**编译失败但没有错误信息**

- 确认 `asm.exe` 是本机可执行文件（不是 0 字节占位文件）。
- 打开“输出”面板选择 `HanASM` 频道查看完整命令行与退出码。
- 在项目根目录手动执行同样的命令行，对比输出。