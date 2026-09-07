# GitHub 发布清单

## 首次建立仓库

在本目录执行：

```powershell
git init
git add .
git commit -m "Release v1.0.0"
git branch -M main
git remote add origin https://github.com/HyGeoIceFairy/BaldrForceEXE-Win11-Fix.git
git push -u origin main
```

推送前应在 GitHub 新建同名空仓库，不要勾选自动生成 README、License 或 `.gitignore`。

## 创建 v1.0.0 Release

1. 在仓库根目录运行：

   ```powershell
   .\scripts\package.ps1 -Version 1.0.0
   ```

2. 在 GitHub 仓库选择 **Releases → Draft a new release**。
3. 新建标签 `v1.0.0`，标题填写 `BALDR FORCE EXE Win11 Fix v1.0.0`。
4. 将 `RELEASE_NOTES.md` 的内容粘贴为发布说明。
5. 上传：
   - `dist/BaldrForceEXE-Win11-Fix-v1.0.0.zip`
   - `dist/BaldrForceEXE-Win11-Fix-v1.0.0.zip.sha256`
6. 发布前再次确认 ZIP 不含游戏程序、汉化内容、存档或游戏资源。

## 建议的仓库设置

- About：`Windows 10/11 compatibility fix for a specific BALDR FORCE EXE Chinese wrapper build`
- Topics：`baldr-force-exe`、`directdraw`、`dgvoodoo2`、`windows-11`、`compatibility-fix`
- Issues：启用；仓库已经提供错误报告模板。
- Discussions：可选，用于集中收集不同系统配置的兼容反馈。

不要把本机游戏目录整体拖入 GitHub Desktop，也不要在游戏根目录直接执行 `git init`。只发布当前隔离仓库目录。
