# 构建启动器

## 环境

- Windows 10/11 x64
- MinGW-w64 GCC，目标架构为 x86_64
- PowerShell 5.1 或 PowerShell 7

## 本地构建

确保 `gcc.exe` 位于 `PATH`，然后在仓库根目录运行：

```powershell
.\scripts\build.ps1
```

也可以指定编译器路径：

```powershell
.\scripts\build.ps1 -Compiler 'C:\mingw64\bin\gcc.exe'
```

输出文件为 `build/Start-BaldrForce.exe`。

等价编译参数：

```text
-std=c11 -Os -Wall -Wextra -municode -mwindows -Wl,--no-insert-timestamp -s
```

`--no-insert-timestamp` 用于移除 PE 构建时间差异，使相同工具链的重复构建可复现。

## 创建发布包

仓库中的 `Start-BaldrForce.exe` 是实际验证过的发布二进制。创建压缩包：

```powershell
.\scripts\package.ps1 -Version 1.0.0
```

脚本采用显式文件白名单，不会扫描或打包仓库外的游戏目录。输出位于 `dist/`，包括最终 ZIP 和对应的 `.sha256` 文件。
