# 发布流程

Windows 便携发布由 `.github/workflows/windows-portable.yml` 生成。`main` 和面向
`main` 的 PR 只构建、测试并保存测试结果；只有 `v*` tag 会打包并创建 GitHub
Release。PR 工作流没有仓库写权限。

## 版本与通道

- `src/PlotJuggler/STUDIO_VERSION` 是唯一版本源，tag 必须逐字符等于
  `v<STUDIO_VERSION>`，否则流水线立即失败。
- 常规版本进入 `stable`；版本中带独立 `beta` 或 `rc` 标识的版本进入 `beta`
  并标记为 GitHub prerelease。
- 不复用版本号，也不移动已发布 tag。每次发布递增版本并创建新 tag。
- 仓库只保留最新版：新 Release 成功公开后，工作流删除所有其他 Release 及旧
  `v*` 发布标签。本地源码历史仍保留，可从提交重新构建旧版本。

## 发布步骤

1. 从受保护的 `main` 创建版本变更，确认 CTest、便携 smoke test、updater E2E
   和发布阻塞项状态。
2. 在本地运行打包与复验：
   `package-portable-windows.ps1`，随后运行 `verify-release.ps1 -Tag v<版本>`。
3. 创建与 `STUDIO_VERSION` 严格匹配的 tag。CI 以 Mosaico 关闭的 UCRT64 配置
   重新构建、测试、打包和复验。
4. CI 附加 ZIP、ZIP SHA-256、`SHA256SUMS` 和
   `update-manifest-<channel>.json`，再次复验后公开 Release。
5. Release 公开成功后，CI 删除旧 Release 和旧 `v*` 发布标签，确保下载页和
   `releases/latest` 只指向当前版本。
6. 发布后运行 `.\scripts\sync-latest-release.ps1`，下载并校验线上 ZIP，将最新版
   解压到 `release\portable\RosPlotJugglerStudio`。确认
   `release\CURRENT_VERSION` 与 `STUDIO_VERSION` 一致，并可直接启动
   `release\Run-RosPlotJugglerStudio.cmd`，才算发布完成。
7. 复验线上 manifest、ZIP 大小和 SHA-256，并用上一版本执行一次在线升级。

## 回滚

- tag 构建失败：修复源码并递增版本后创建新 tag；不要静默替换已公开版本资产。
- 已发布版本有问题：立即删除有问题的 Release 和 tag，修复后发布递增版本。由于
  只保留最新版，不依赖 GitHub Release 保存旧安装包；客户端安装失败仍使用本地
  backup 回滚。
- 客户端安装失败会由外置 updater 恢复同级 backup；保留 updater 日志和失败资产
  用于调查，不能把应用内回滚当成发布流程回滚的替代。

发布签名密钥、证书私钥和密码只允许存放在组织密钥管理系统或受保护的 GitHub
Environment secrets 中，不得写入仓库、构建缓存、日志或 artifact。
