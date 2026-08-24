# 贡献指南

## 开发流程

1. 从最新 `main` 创建短生命周期分支；一个 PR 聚焦一个问题。
2. 遵守现有 C++17、Qt5、CMake 和 PowerShell 风格，保留适用的 SPDX 标识。
3. 不提交 `build/`、`install/`、`release/`、`.toolchain/`、`artifacts/`、日志、
   IDE 配置、凭证或大型生成 fixture。必要测试 fixture 应最小化并说明来源、
   许可证和大小。
4. Windows 变更至少运行 `scripts/build-windows.ps1`。打包/updater 变更还需运行
   `package-portable-windows.ps1`、`verify-release.ps1` 和相关 E2E 测试。
   Mosaico 默认关闭；启用后的结果应单独说明。
5. 更新受影响文档，说明已验证和未验证范围。不要把编译通过表述为真实 bag 或
   完整 GUI 已验收。

PR 应包含变更目的、风险、测试命令及结果、用户可见影响和回滚方式。依赖或上游
同步必须记录精确版本/提交、来源、许可证影响和 Studio 冲突处理。

## 安全与发布

安全问题按 `SECURITY.md` 私密报告。贡献者不得把 token、证书私钥、签名密钥或
用户数据放进代码、fixture、Issue、日志或 CI artifact。发布由维护者按
`docs/RELEASE_PROCESS.md` 完成；普通 PR 不应请求 `contents: write` 或发布
secrets。
