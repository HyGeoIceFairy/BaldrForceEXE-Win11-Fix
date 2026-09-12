# BALDR FORCE EXE Win11 Fix

面向特定《BALDR FORCE EXE》中文封装版本的非官方 Windows 10/11 兼容补丁。

补丁不包含游戏本体、汉化程序、汉化文本、音效、语音、图片、脚本、存档或其他游戏资源。使用者需要自行准备合法取得且已完整安装的游戏。

## 解决的问题

适用症状包括：

- 启动时提示 DirectDraw 初始化失败；
- 加入旧式 DirectDraw 兼容层后黑屏；
- 中文封装版本启动时错误报告音效目录不存在；
- 进入标题菜单时因音效资源未正确注册而退出。

补丁包含两个互相配合的部分：

1. 使用 dgVoodoo2 的 32 位 DirectDraw 包装器，把旧式 DirectDraw 输出转换到现代 Windows 图形 API。
2. 使用兼容启动器建立音效资源别名，并在游戏解包完成后临时修正进程内的资源目录名，使游戏自己的文件枚举和音效注册流程正常执行。

从 1.1.0 起，启动器还提供原生中文设置面板，可在启动前选择窗口化、无边框全屏或传统独占全屏，并管理窗口尺寸和常用显示选项。

所有运行时修改只存在于游戏进程内存中，不会改写磁盘上的原游戏程序。

## 适用版本

当前版本仅针对以下文件完成验证：

- 游戏程序名：`BaldrForce.exe`
- 文件大小：`46,252,058` 字节
- SHA-256：`7A9341D40E24C4728ECC8666FE77BBE01EE78D65C65E7F6F9A486BA9630CAD76`
- 必需资源：游戏目录中存在 `Se\Abort.wav`
- 系统：Windows 10/11 x64

其他版本的地址布局可能不同。启动器会在写入前检查目标代码和资源名；检查不通过时会终止它创建的游戏进程并显示错误，而不是继续写入未知地址。

可在 PowerShell 中检查游戏程序：

```powershell
Get-Item .\BaldrForce.exe | Select-Object Name, Length
Get-FileHash .\BaldrForce.exe -Algorithm SHA256
```

## 安装

1. 从 GitHub Releases 下载最新的 `BaldrForceEXE-Win11-Fix-v*.zip`。
2. 关闭游戏。
3. 如果游戏目录已有 `ddraw.dll` 或 `dgVoodoo.conf`，先把它们备份到游戏目录之外。
4. 把压缩包内以下文件复制到 `BaldrForce.exe` 所在目录：
   - `Start-BaldrForce.exe`
   - `ddraw.dll`
   - `dgVoodoo.conf`
   - `USER_GUIDE.zh-CN.txt`（建议保留，方便从启动面板打开）
5. 双击 `Start-BaldrForce.exe` 启动。以后也使用它进入游戏。

第一次运行会在游戏目录创建 `Sx`。其中的 WAV 在 NTFS 上优先使用硬链接指向原 `Se` 文件，因此通常不会额外占用一整份音效空间；文件系统不支持硬链接时会回退为复制。

首次启动会显示设置面板。点击“保存并启动”后，启动器会创建 `Start-BaldrForce.ini` 来记住选择；第一次需要修改 `dgVoodoo.conf` 时会保留一份 `dgVoodoo.conf.baldrforce-backup`。启动器成功应用补丁和显示模式后会自行退出，游戏继续运行，这是正常行为。

### 从 1.0.0 升级

1. 关闭游戏。
2. 用 1.1.0 的 `Start-BaldrForce.exe` 覆盖旧启动器。
3. `ddraw.dll` 仍为 dgVoodoo2 2.87.4，无需重复覆盖。
4. 建议保留当前 `dgVoodoo.conf`；新版首次打开会读取其中可识别的旧显示设置。旧版全屏意图会迁移为推荐的无边框全屏，只有点击“保存并启动”后才会改动受管理项。
5. 如果曾手动定制 `dgVoodoo.conf`，仍建议先在游戏目录之外留一份副本。启动器会保留不受其管理的章节、选项和注释，但不能代替用户自己的长期备份。

## 显示与便捷功能

### 显示模式

- **窗口化**：可选择 `640×480`、`800×600`、`960×720`、`1024×768`、`1280×960`，也可直接输入 `宽 x 高`。尺寸指游戏客户区，不会提高原始图片或动画精度。
- **无边框全屏（推荐）**：用无边框窗口覆盖游戏当前所在的显示器，不进行独占显示模式切换；通常更适合现代 Windows 的切出和切回。
- **传统独占全屏**：保留旧版由游戏与 dgVoodoo2 控制全屏的行为。不同显卡驱动、DPI 和多显示器组合下的表现可能不同。

窗口尺寸必须在 `320×240` 到 `7680×4320` 之间，并且不能超过当前显示器的可用工作区。默认保持原始宽高比；关闭后可拉伸填满窗口或屏幕。

### 其他设置

- 平滑缩放；关闭后使用清晰像素缩放。
- 限制鼠标在游戏画面内。
- DirectX 输出垂直同步。
- 窗口在当前显示器居中。
- 记住设置并以后直接启动。

启用快速启动后，可按住 **Shift** 再双击启动器来重新打开设置，也可以创建带 `--settings` 参数的快捷方式。`--launch` 强制按已保存设置启动；`--diagnose` 只显示诊断摘要，不启动游戏。

面板还提供打开游戏目录、使用指南、项目发布页、恢复推荐设置和复制诊断摘要的入口。诊断摘要不会自动上传，也不读取存档。

## 使用注意

- 不要删除或改名原始 `Se` 目录。
- 不要把其他用途的现有 `Sx` 目录交给启动器使用。启动器只维护带有隐藏标记 `Sx\.baldrforce-se-alias` 的目录。
- 不要直接修改 `Sx` 中的 WAV；在 NTFS 上它们通常与 `Se` 中的文件共享数据。
- 移动游戏时请移动整个游戏目录。若没有复制 `Sx`，下次启动时会自动重建。
- 不需要启用 DirectPlay；本补丁处理的是图形兼容和资源路径问题。
- 不要在游戏运行时修改显示设置。新版会检测同一目录中已经运行的游戏，不会终止该进程或启动第二份。
- `Start-BaldrForce.log` 只记录启动阶段和 Windows 错误码；可在排障完成后直接删除。

## 卸载

1. 关闭游戏和启动器。
2. 删除补丁提供的 `Start-BaldrForce.exe`、`ddraw.dll` 和 `dgVoodoo.conf`。
3. 可以删除启动器生成的 `Start-BaldrForce.ini`、`Start-BaldrForce.log` 和 `dgVoodoo.conf.baldrforce-backup`；如果备份中包含自己的高级设置，请先确认不再需要。
4. 仅当 `Sx` 内存在隐藏目录 `.baldrforce-se-alias` 时，才删除整个 `Sx`。
5. 如果安装前备份过同名兼容层文件，把备份恢复原名。

补丁不会改写 `BaldrForce.exe`，因此无需恢复游戏程序。

## 常见问题

### 启动器报告版本不匹配或等待解包超时

先核对上方文件大小和 SHA-256。本补丁不能安全应用到地址布局不同的程序。

### 启动器报告无法建立 Sx

确认 `Se\Abort.wav` 存在、游戏目录可写，并检查是否已经存在不属于本补丁的 `Sx`。不要直接覆盖个人文件；先把冲突目录移走再运行。

### 安全软件报警

兼容启动器需要创建游戏进程、读取少量内存、短暂暂停线程并写入两个字节。此行为可能触发启发式检测。请只从本仓库 Releases 下载，并对照发布页的 SHA-256；不要在关闭系统安全防护的情况下运行来源不明的副本。

### 画面比例或全屏行为不合适

先按住 Shift 打开设置，选择“无边框全屏（推荐）”并保持原始宽高比。窗口模式超出屏幕时请选择更小的尺寸。高级用户仍可调整 `dgVoodoo.conf`；启动器只维护面板对应的显示项，其他内容会保留。

### 启用快速启动后如何重新打开设置

按住 Shift 再双击 `Start-BaldrForce.exe`，或从命令行执行 `Start-BaldrForce.exe --settings`。

### 启动器报告设置或兼容层配置无效

不要直接覆盖未知配置。先查看错误提示和 `Start-BaldrForce.log`。如需重新生成启动器设置，可把 `Start-BaldrForce.ini` 移到其他目录后重新打开；如需恢复首次修改前的兼容层配置，可先关闭游戏，再核对并使用 `dgVoodoo.conf.baldrforce-backup`。

## 仓库内容

- `src/Start-BaldrForce.c`：兼容启动流程与窗口模式源码。
- `src/launcher_config.c`：设置与 dgVoodoo 配置管理源码。
- `src/launcher_ui.c`：原生中文启动面板源码。
- `Start-BaldrForce.exe`：已验证的 64 位 Windows 启动器。
- `config/dgVoodoo.conf`：已验证的 dgVoodoo2 配置。
- `third_party/dgVoodoo2/ddraw.dll`：dgVoodoo2 v2.87.4 的 32 位 DirectDraw 包装器。
- `docs/USER_GUIDE.zh-CN.txt`：随发布包提供的简体中文使用指南。
- `scripts/build.ps1`：本地构建脚本。
- `scripts/package.ps1`：按白名单创建最终用户发布包。
- `scripts/test.ps1`、`scripts/test-package.ps1`：原生配置与发布包测试。

## 许可与声明

本项目自有源码、脚本和文档采用 MIT License，详见 [LICENSE](LICENSE)。

`ddraw.dll` 和 dgVoodoo 配置属于第三方组件，不适用 MIT License；详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。dgVoodoo2 作者允许游戏或游戏 Mod 随附所需的单个文件。

《BALDR FORCE EXE》及其相关名称、程序和资源的权利归各自权利人所有。本项目是非官方兼容补丁，与游戏开发商、发行商及汉化制作者无隶属或背书关系。
