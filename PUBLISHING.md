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

## 创建版本 Release

1. 在仓库根目录运行：

   ```powershell
   .\scripts\build.ps1
   .\scripts\test.ps1
   .\scripts\test-package.ps1
   .\scripts\package.ps1 -Version 1.1.0
   ```

2. 阅读 `docs/VALIDATION-<版本>.md` 的发布结论；只要其中仍有未完成的实机矩阵或交互验收，GitHub Release 必须勾选 **Set as a pre-release**，并在发布说明中披露缺口。全部门禁完成后才可发布为稳定版。
3. 在 GitHub 仓库选择 **Releases → Draft a new release**。
4. 新建对应语义版本标签（例如 `v1.1.0`），标题填写同版本的 `BALDR FORCE EXE Win11 Fix`。
5. 将 `RELEASE_NOTES.md` 的内容粘贴为发布说明。
6. 上传：
   - 对应版本的 `dist/BaldrForceEXE-Win11-Fix-v*.zip`
   - 对应版本的 `dist/BaldrForceEXE-Win11-Fix-v*.zip.sha256`
7. 发布前再次确认 ZIP 不含游戏程序、汉化内容、存档或游戏资源。
8. 发布后下载两个附件，重新计算 ZIP SHA-256，并核对标签提交、附件名、摘要和预发布状态。

## 建议的仓库设置

- About：`Windows 10/11 compatibility fix for a specific BALDR FORCE EXE Chinese wrapper build`
- Topics：`baldr-force-exe`、`directdraw`、`dgvoodoo2`、`windows-11`、`compatibility-fix`
- Issues：启用；仓库已经提供错误报告模板。
- Discussions：可选，用于集中收集不同系统配置的兼容反馈。

不要把本机游戏目录整体拖入 GitHub Desktop，也不要在游戏根目录直接执行 `git init`。只发布当前隔离仓库目录。
