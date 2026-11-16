import * as fs from 'fs';
import * as path from 'path';
import { spawn } from 'child_process';
import { TextDecoder } from 'util';
import * as vscode from 'vscode';

const LANGUAGE_ID = 'han-asm';
const OUTPUT_CHANNEL_NAME = 'HanASM';
const EXECUTABLE_NAME = process.platform === 'win32' ? 'asm.exe' : 'asm';

let outputChannel: vscode.OutputChannel;
let statusBarItem: vscode.StatusBarItem;
let isBuilding = false;

export function activate(context: vscode.ExtensionContext): void {
  outputChannel = vscode.window.createOutputChannel(OUTPUT_CHANNEL_NAME);

  statusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 8);
  statusBarItem.command = 'han-asm.build';

  context.subscriptions.push(
    outputChannel,
    statusBarItem,
    vscode.commands.registerCommand('han-asm.build', (uri?: vscode.Uri) => {
      void runBuildCommand(uri);
    }),
    vscode.window.onDidChangeActiveTextEditor(() => refreshStatusBar()),
    vscode.workspace.onDidOpenTextDocument(() => refreshStatusBar()),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (event.affectsConfiguration('hanAsm')) {
        refreshStatusBar();
      }
    })
  );

  refreshStatusBar();
}

export function deactivate(): void {
  // 无需显式清理：所有 Disposable 都已注册到 context.subscriptions。
}

/**
 * 命令入口：优先用右键菜单/标题栏传入的 uri，其次当前活动编辑器；
 * 都没有时弹出文件选择框，保证命令面板里任何时候都可调用。
 */
async function runBuildCommand(uri?: vscode.Uri): Promise<void> {
  let target = uri ?? vscode.window.activeTextEditor?.document.uri;

  if (!target) {
    const picked = await vscode.window.showOpenDialog({
      canSelectMany: false,
      openLabel: '编译',
      title: '选择要编译的中文汇编源文件',
      filters: { '中文汇编源文件': ['asm', 'hasm', 'hanasm'] }
    });
    if (!picked || picked.length === 0) {
      return; // 用户取消选择，静默退出
    }
    target = picked[0];
  }

  await build(target);
}

/**
 * 编译给定的源文件：保存 -> 定位 asm.exe -> spawn 调用 -> 输出结果。
 */
async function build(target: vscode.Uri): Promise<void> {
  if (isBuilding) {
    void vscode.window.showInformationMessage('HanASM：正在编译，请稍候…');
    return;
  }

  if (target.scheme !== 'file') {
    void vscode.window.showWarningMessage('HanASM：请选择一个磁盘上的源文件（暂不支持未保存的虚拟文档）。');
    return;
  }

  const filePath = target.fsPath;
  if (!isFile(filePath)) {
    void vscode.window.showWarningMessage(`HanASM：找不到文件 ${filePath}。`);
    return;
  }

  const config = vscode.workspace.getConfiguration('hanAsm', target);

  // 编译前保存：未保存的改动不会被编译器读到。
  const document = vscode.workspace.textDocuments.find(
    (item) => item.uri.toString() === target.toString()
  );
  if (document?.isDirty && config.get<boolean>('saveBeforeBuild', true)) {
    const saved = await document.save();
    if (!saved) {
      void vscode.window.showErrorMessage('HanASM：文件保存失败，已取消编译。');
      return;
    }
  }

  const executable = resolveExecutable(filePath, config);
  if (!executable) {
    return;
  }

  const template = config.get<string[]>('arguments') ?? ['${file}'];
  const args = template.map((value) => expandTemplate(value, filePath));

  if (config.get<string>('output', 'outputChannel') === 'terminal') {
    runInTerminal(executable, args, filePath);
    return;
  }

  await runAndCapture(executable, args, filePath, config);
}

/**
 * 查找 asm.exe：配置路径 -> 源文件目录 -> 工作区根目录/bin -> PATH。
 */
function resolveExecutable(
  filePath: string,
  config: vscode.WorkspaceConfiguration
): string | undefined {
  const fileDir = path.dirname(filePath);
  const configured = (config.get<string>('executablePath') ?? '').trim();

  if (configured.length > 0) {
    const expanded = expandTemplate(configured, filePath);
    const candidates = path.isAbsolute(expanded)
      ? [expanded]
      : [path.resolve(fileDir, expanded), path.resolve(expanded)];
    const found = candidates.find(isFile);
    if (found) {
      return found;
    }
    void promptOpenSetting(
      `HanASM：设置项 hanAsm.executablePath 指向的 "${configured}" 不存在。`
    );
    return undefined;
  }

  const candidates: string[] = [
    path.join(fileDir, EXECUTABLE_NAME),
    path.join(fileDir, 'bin', EXECUTABLE_NAME)
  ];

  for (const folder of vscode.workspace.workspaceFolders ?? []) {
    candidates.push(path.join(folder.uri.fsPath, EXECUTABLE_NAME));
    candidates.push(path.join(folder.uri.fsPath, 'bin', EXECUTABLE_NAME));
  }

  for (const entry of (process.env.PATH ?? '').split(path.delimiter)) {
    if (entry.trim().length > 0) {
      candidates.push(path.join(entry.trim(), EXECUTABLE_NAME));
    }
  }

  const found = candidates.find(isFile);
  if (found) {
    return found;
  }

  void promptOpenSetting(
    `HanASM：未找到 ${EXECUTABLE_NAME}。请把它放到源文件同目录，或设置 hanAsm.executablePath。`
  );
  return undefined;
}

/**
 * 在集成终端里直接启动编译器（不解析退出码）。
 */
function runInTerminal(executable: string, args: string[], filePath: string): void {
  const terminal = vscode.window.createTerminal({
    name: `HanASM · ${path.basename(filePath)}`,
    cwd: path.dirname(filePath),
    shellPath: executable,
    shellArgs: args
  });
  terminal.show();
  outputChannel.appendLine(
    `[${clock()}] ▶ 在终端中编译：${path.basename(filePath)}（${[executable, ...args].join(' ')}）`
  );
}

/**
 * 启动编译器、收集输出并写入 OutputChannel。
 */
async function runAndCapture(
  executable: string,
  args: string[],
  filePath: string,
  config: vscode.WorkspaceConfiguration
): Promise<void> {
  const cwd = path.dirname(filePath);
  const encoding = config.get<string>('encoding', 'auto');
  const startedAt = Date.now();

  isBuilding = true;
  refreshStatusBar();
  outputChannel.show(true);
  outputChannel.appendLine('');
  outputChannel.appendLine(`[${clock()}] ▶ 编译 ${path.basename(filePath)}`);
  outputChannel.appendLine(`    工作目录：${cwd}`);
  outputChannel.appendLine(`    命令：${[executable, ...args].map(quote).join(' ')}`);

  try {
    const result = await spawnAndCollect(executable, args, cwd);
    const stdout = decode(result.stdout, encoding);
    const stderr = decode(result.stderr, encoding);

    if (stdout.trim().length > 0) {
      outputChannel.appendLine(stdout.replace(/\s+$/, ''));
    }
    if (stderr.trim().length > 0) {
      outputChannel.appendLine('--- 标准错误 ---');
      outputChannel.appendLine(stderr.replace(/\s+$/, ''));
    }

    const elapsed = Date.now() - startedAt;
    if (result.code === 0) {
      outputChannel.appendLine(`[${clock()}] ✔ 编译成功（${elapsed} ms）`);
      void vscode.window.setStatusBarMessage(`$(check) HanASM 编译成功（${elapsed} ms）`, 4000);
    } else {
      outputChannel.appendLine(
        `[${clock()}] ✘ 编译失败（退出码 ${result.code ?? '未知'}，${elapsed} ms）`
      );
      const choice = await vscode.window.showErrorMessage(
        `HanASM 编译失败（退出码 ${result.code ?? '未知'}）。`,
        '查看输出'
      );
      if (choice === '查看输出') {
        outputChannel.show(true);
      }
    }
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    outputChannel.appendLine(`[${clock()}] ✘ 无法启动编译器：${message}`);
    const choice = await vscode.window.showErrorMessage(
      `HanASM 无法启动编译器：${message}`,
      '查看输出',
      '打开设置'
    );
    if (choice === '查看输出') {
      outputChannel.show(true);
    } else if (choice === '打开设置') {
      void vscode.commands.executeCommand('workbench.action.openSettings', 'hanAsm.executablePath');
    }
  } finally {
    isBuilding = false;
    refreshStatusBar();
  }
}

interface SpawnResult {
  code: number | null;
  stdout: Buffer;
  stderr: Buffer;
}

function spawnAndCollect(executable: string, args: string[], cwd: string): Promise<SpawnResult> {
  return new Promise<SpawnResult>((resolve, reject) => {
    const child = spawn(executable, args, {
      cwd,
      windowsHide: true,
      stdio: ['ignore', 'pipe', 'pipe']
    });

    const stdoutChunks: Buffer[] = [];
    const stderrChunks: Buffer[] = [];

    child.stdout?.on('data', (chunk: Buffer) => stdoutChunks.push(chunk));
    child.stderr?.on('data', (chunk: Buffer) => stderrChunks.push(chunk));
    child.on('error', reject);
    child.on('close', (code) => {
      resolve({
        code,
        stdout: Buffer.concat(stdoutChunks),
        stderr: Buffer.concat(stderrChunks)
      });
    });
  });
}

/**
 * 解码编译器输出：auto 模式先试 UTF-8，失败则退回 GBK。
 */
function decode(buffer: Buffer, encoding: string): string {
  if (buffer.length === 0) {
    return '';
  }
  if (encoding !== 'auto') {
    try {
      return new TextDecoder(encoding).decode(buffer);
    } catch {
      // 当前 Node 不支持该编码，继续走自动探测。
    }
  }
  try {
    return new TextDecoder('utf-8', { fatal: true }).decode(buffer);
  } catch {
    // 不是合法 UTF-8，尝试中文 Windows 常见的 GBK。
  }
  try {
    return new TextDecoder('gbk').decode(buffer);
  } catch {
    return buffer.toString('utf8');
  }
}

/**
 * 展开 ${file} 等变量。
 */
function expandTemplate(value: string, filePath: string): string {
  const fileDir = path.dirname(filePath);
  const parsed = path.parse(filePath);
  const workspaceFolder =
    vscode.workspace.getWorkspaceFolder(vscode.Uri.file(filePath))?.uri.fsPath ?? fileDir;

  return value
    .replace(/\$\{file\}/g, filePath)
    .replace(/\$\{fileDirname\}/g, fileDir)
    .replace(/\$\{fileBasename\}/g, parsed.base)
    .replace(/\$\{fileBasenameNoExtension\}/g, parsed.name)
    .replace(/\$\{fileExtname\}/g, parsed.ext)
    .replace(/\$\{workspaceFolder\}/g, workspaceFolder);
}

function refreshStatusBar(): void {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== LANGUAGE_ID) {
    statusBarItem.hide();
    return;
  }

  if (isBuilding) {
    statusBarItem.text = '$(sync~spin) 正在编译 HanASM';
  } else {
    statusBarItem.text = '$(tools) 编译 HanASM';
  }
  statusBarItem.tooltip = `编译 ${path.basename(editor.document.fileName)}（Ctrl+Alt+B）`;
  statusBarItem.show();
}

function promptOpenSetting(message: string): Thenable<string | undefined> {
  return vscode.window
    .showErrorMessage(message, '打开设置')
    .then((choice) => {
      if (choice === '打开设置') {
        void vscode.commands.executeCommand(
          'workbench.action.openSettings',
          'hanAsm.executablePath'
        );
      }
      return choice;
    });
}

function isFile(candidate: string): boolean {
  try {
    return fs.statSync(candidate).isFile();
  } catch {
    return false;
  }
}

function quote(value: string): string {
  return /[\s"]/u.test(value) ? `"${value.replace(/"/g, '\\"')}"` : value;
}

function clock(): string {
  const now = new Date();
  const pad = (value: number): string => String(value).padStart(2, '0');
  return `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
}