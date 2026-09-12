# BALDR FORCE EXE Win11 Fix v1.1.0

面向特定《BALDR FORCE EXE》中文封装版本的非官方 Windows 10/11 兼容补丁。

> 本版本先作为预发布版提供。窗口化与推荐的无边框全屏已在 Windows 11、2560×1600、
> 150% DPI 的目标环境通过实测；传统独占全屏、多显示器、其他 DPI／驱动组合以及完整
> 战斗流程仍需要更多用户环境验证。

## 主要更新

- 新增原生中文启动面板，可选窗口化、无边框全屏和传统独占全屏。
- 窗口模式提供常用分辨率和自定义客户区尺寸；无边框全屏是现代 Windows 上的推荐模式。
- 可调整宽高比、平滑／清晰缩放、鼠标限制、垂直同步和窗口居中。
- 可记住设置并以后直接启动；按住 Shift 可随时重新打开设置。
- 新增恢复推荐设置、目录／指南／发布页入口，以及不会自动上传的诊断摘要和启动日志。
- 首次修改 `dgVoodoo.conf` 前自动创建 `dgVoodoo.conf.baldrforce-backup`，保留未受管理的高级设置。
- 加强游戏版本预检、重复实例保护和进程内补丁二次校验。

原有 DirectDraw 和 `Se` 音效资源目录兼容修复保持不变；仍不会修改磁盘上的游戏程序或存档。

## 安装

1. 下载 `BaldrForceEXE-Win11-Fix-v1.1.0.zip`。
2. 新安装：把 `Start-BaldrForce.exe`、`ddraw.dll`、`dgVoodoo.conf` 和 `USER_GUIDE.zh-CN.txt` 放到 `BaldrForce.exe` 所在目录。
3. 双击 `Start-BaldrForce.exe` 启动游戏。

从 1.0.0 升级时只需覆盖 `Start-BaldrForce.exe`，并建议保留当前 `dgVoodoo.conf`。如果曾手工修改兼容层配置，请先另行备份。

适用游戏程序 SHA-256：

```text
7A9341D40E24C4728ECC8666FE77BBE01EE78D65C65E7F6F9A486BA9630CAD76
```

完整安装、升级、显示模式、设置恢复、卸载、适用范围和安全说明见仓库 `README.md` 及压缩包内的 `USER_GUIDE.zh-CN.txt`。

本补丁不包含游戏本体、汉化内容或任何游戏资源。
