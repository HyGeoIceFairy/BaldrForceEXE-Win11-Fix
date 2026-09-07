# BALDR FORCE EXE Win11 Fix v1.0.0

面向特定《BALDR FORCE EXE》中文封装版本的非官方 Windows 10/11 兼容补丁。

## 修复内容

- 通过 dgVoodoo2 修复现代 Windows 上的 DirectDraw 初始化问题。
- 修复中文封装版本无法正常读取 `Se` 音效目录的问题。
- 保留游戏原有的音效文件枚举和资源注册流程。
- 不修改磁盘上的游戏程序或存档。

## 安装

1. 下载 `BaldrForceEXE-Win11-Fix-v1.0.0.zip`。
2. 解压后，把 `Start-BaldrForce.exe`、`ddraw.dll` 和 `dgVoodoo.conf` 放到 `BaldrForce.exe` 所在目录。
3. 双击 `Start-BaldrForce.exe` 启动游戏。

适用游戏程序 SHA-256：

```text
7A9341D40E24C4728ECC8666FE77BBE01EE78D65C65E7F6F9A486BA9630CAD76
```

完整安装、卸载、适用范围和安全说明见仓库 `README.md` 及压缩包内的 `USER_GUIDE.zh-CN.txt`。

本补丁不包含游戏本体、汉化内容或任何游戏资源。
