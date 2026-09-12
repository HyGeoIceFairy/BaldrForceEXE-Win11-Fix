# 构建启动器

## 环境

- Windows 10/11 x64
- MinGW-w64 GCC，目标架构为 x86_64
- 与 GCC 同目录的 `windres.exe`
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

运行原生配置测试：

```powershell
.\scripts\test.ps1
```

测试发布包白名单、当前构建产物和拒绝覆盖行为：

```powershell
.\scripts\test-package.ps1
```

等价编译参数：

```text
-std=c11 -Os -Wall -Wextra -Wpedantic -Werror -municode -mwindows -Wl,--no-insert-timestamp -s
```

`--no-insert-timestamp` 用于移除 PE 构建时间差异，使相同工具链的重复构建可复现。

## 创建发布包

打包脚本只使用本次构建得到的 `build/Start-BaldrForce.exe`，不会使用仓库根目录中的旧二进制。先完成构建和测试，再创建压缩包：

```powershell
.\scripts\package.ps1 -Version 1.1.0
```

脚本采用显式文件白名单，不会扫描或打包仓库外的游戏目录。每次使用唯一暂存目录，已有同名发布包时会拒绝覆盖，并核对实际 ZIP 条目。输出位于 `dist/`，包括最终 ZIP 和对应的 `.sha256` 文件。
