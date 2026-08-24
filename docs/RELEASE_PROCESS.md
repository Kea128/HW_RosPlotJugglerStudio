# 发布流程

Windows 便携发布由 `.github/workflows/windows-portable.yml` 生成。`main` 和面向
`main` 的 PR 只构建、测试并保存测试结果；只有 `v*` tag 会打包并创建 GitHub
draft Release。PR 工作流没有仓库写权限。

## 版本与通道

- `src/PlotJuggler/STUDIO_VERSION` 是唯一版本源，tag 必须逐字符等于
  `v<STUDIO_VERSION>`，否则流水线立即失败。
- 常规版本进入 `stable`；版本中带独立 `beta` 或 `rc` 标识的版本进入 `beta`
  并标记为 GitHub prerelease。
- 不复用版本号，也不移动已发布 tag。版本或通道错误时应撤销 draft、修正版本后
  创建新 tag。

## 发布步骤

1. 从受保护的 `main` 创建版本变更，确认 CTest、便携 smoke test、updater E2E
   和发布阻塞项状态。
2. 在本地运行打包与复验：
   `package-portable-windows.ps1`，随后运行 `verify-release.ps1 -Tag v<版本>`。
3. 创建与 `STUDIO_VERSION` 严格匹配的 tag。CI 以 Mosaico 关闭的 UCRT64 配置
   重新构建、测试、打包和复验。
4. CI 仅创建 draft Release，并附加 ZIP、ZIP SHA-256、`SHA256SUMS` 和
   `update-manifest-<channel>.json`。
5. 发布人员下载 draft 资产，在独立 Windows 机器复验 SHA-256、启动、插件加载、
   ROS1/ROS2 bag、更新与回滚。确认 manifest 的 URL 指向该 tag 的同名资产。
6. 验收记录经第二位维护者复核后，人工点击 Publish。不要让自动化直接发布。

## 回滚

- draft 有问题：删除或替换 draft 资产，修复源码并创建新版本/tag；不要静默替换
  已公开版本的 ZIP。
- 已发布版本有问题：将受影响 Release 标为 prerelease 或在说明中明确撤回，恢复
  上一个已验证 manifest，并发布递增的修复版本。
- 客户端安装失败会由外置 updater 恢复同级 backup；保留 updater 日志和失败资产
  用于调查，不能把应用内回滚当成发布流程回滚的替代。

发布签名密钥、证书私钥和密码只允许存放在组织密钥管理系统或受保护的 GitHub
Environment secrets 中，不得写入仓库、构建缓存、日志或 artifact。
