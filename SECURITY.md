# 安全说明

## 启动器行为

`Start-BaldrForce.exe` 会：

1. 在自身目录检查 `BaldrForce.exe`、`ddraw.dll`、`dgVoodoo.conf` 和 `Se\Abort.wav`。
2. 显示本地设置面板；保存时写入 `Start-BaldrForce.ini`，并原子更新受管理的 dgVoodoo 显示项。
3. 第一次实际修改 `dgVoodoo.conf` 前创建 `dgVoodoo.conf.baldrforce-backup`，后续保存不覆盖首次备份。
4. 创建或维护带有所有权标记的 `Sx` 音效别名目录。
5. 检查同一目录中是否已有游戏实例，然后启动 `BaldrForce.exe`。
6. 读取目标进程中的固定签名并验证版本状态。
7. 短暂暂停游戏主线程，再次核对目标字节后，把资源目录名的两个字节由 `Se` 改为 `Sx`，随后恢复线程。
8. 对窗口化或无边框模式调整游戏窗口，并在完成后退出。

它不会上传数据，不自动下载更新，不修改磁盘上的游戏程序，也不读取或写入存档。只有用户点击“发布页”时，启动器才会把项目 Releases 地址交给系统默认浏览器打开。

`Start-BaldrForce.log` 只记录本地时间、启动阶段和 Windows 错误码，不包含存档内容，也不会自动上传。面板中的诊断摘要只报告补丁版本、操作系统、必要文件是否存在、游戏程序大小和当前显示选项。

## 文件校验

GitHub Release 中的 `SHA256SUMS.txt` 记录发布文件哈希。建议下载后执行：

```powershell
Get-FileHash .\Start-BaldrForce.exe -Algorithm SHA256
Get-FileHash .\ddraw.dll -Algorithm SHA256
```

## 报告安全问题

报告时请提供补丁版本、Windows 版本、错误信息和复现步骤。不要上传游戏程序、汉化文件、存档或其他受版权保护的资源。
