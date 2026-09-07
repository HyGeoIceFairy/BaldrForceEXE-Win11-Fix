# 安全说明

## 启动器行为

`Start-BaldrForce.exe` 会：

1. 在自身目录检查 `BaldrForce.exe` 和 `Se\Abort.wav`。
2. 创建或维护带有所有权标记的 `Sx` 音效别名目录。
3. 启动 `BaldrForce.exe`。
4. 读取目标进程中的固定签名并验证版本状态。
5. 短暂暂停游戏主线程，把资源目录名的两个字节由 `Se` 改为 `Sx`，随后恢复线程并退出。

它不会上传数据，不访问网络，不修改磁盘上的游戏程序，也不读取或写入存档。

## 文件校验

GitHub Release 中的 `SHA256SUMS.txt` 记录发布文件哈希。建议下载后执行：

```powershell
Get-FileHash .\Start-BaldrForce.exe -Algorithm SHA256
Get-FileHash .\ddraw.dll -Algorithm SHA256
```

## 报告安全问题

报告时请提供补丁版本、Windows 版本、错误信息和复现步骤。不要上传游戏程序、汉化文件、存档或其他受版权保护的资源。
