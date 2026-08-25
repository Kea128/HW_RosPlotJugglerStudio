# RosPlotJuggler Studio

RosPlotJuggler Studio 是基于 PlotJuggler 3.17.2 的二次开发工作区，目标是在保留
上游数据加载、流式输入、解析、绘图、变换和插件能力的基础上，逐步增加面向
ROS 离线分析的 Studio 工作流。

## 当前状态

本工作区已恢复并接回 2026-08-19 前完成的 Studio 二次开发，同时保留在线更新、
ROS bag 和 Windows 便携发布能力：

- PlotJuggler 上游基线固定为
  [`034a5cc919909aebb124e0dc1abbe2029c3eed0f`](https://github.com/facontidavide/PlotJuggler/commit/034a5cc919909aebb124e0dc1abbe2029c3eed0f)
  （3.17.2）。
- 已从该固定提交恢复可确认的空白上游文件，未覆盖现有非空 Studio 定制。
- 现有 `build/`、`install/`、`release/`、`.toolchain/` 和运行时目录是本地生成或
  分发产物，不是源码替代品。
- `src/PlotJuggler/STUDIO_VERSION` 是 CMake 唯一版本源；当前版本为
  `3.17.3`。
- 已恢复双标尺 A/B、直接拖动、可见曲线测量表、交点值标签与避让、时间域智能
  fit、加载后自动 fit、Studio 品牌及历史 MQTT/ZMQ 可靠性修复。
- Windows UCRT64 构建产物为 `RosPlotJugglerStudio.exe`，插件安装到
  `bin/plugins`。
- Windows UCRT64 构建和 108 项 CTest
  已在当前机器实际完成；确定性 ROS1 fixture 已通过 Python worker 与记录解析回归。
- `main`/PR 会在 GitHub Actions 使用 MSYS2 UCRT64 构建测试；严格匹配
  `STUDIO_VERSION` 的 `v*` tag 会生成、复验并公开最新版 Release；发布成功后
  自动删除旧 Release 和旧 `v*` 发布标签。
- 现场采集的 ROS1/ROS2 bag、完整 GUI 和全部插件行为仍需单独验收。

详细信息见 [UPSTREAM.md](UPSTREAM.md)、
[项目状态](docs/PROJECT_STATUS.md) 和
[功能证据清单](docs/FEATURE_PARITY.md)。Windows 外置更新器的协议、安全解压、
回滚和发布清单格式见 [Windows 自动更新](docs/UPDATER.md)。发布和日常维护见
[发布流程](docs/RELEASE_PROCESS.md) 与 [维护指南](docs/MAINTENANCE.md)。

## 目录

- `src/PlotJuggler/`：固定上游基线与保留的 Studio 修改。
- `src/plotjuggler-ros-plugins/`：独立 ROS 插件源码。
- `docs/`：产品设计、状态和功能证据。
- `scripts/`：Windows 构建、运行、ROS bag runtime、便携打包和 smoke test。
- `build/`、`install/`、`release/`：生成物，均被根 `.gitignore` 忽略。

## Windows 构建与打包

默认工具链从仓库根的 `.toolchain/msys64` 解析；所有主要目录和并发数均可通过
脚本参数覆盖，不依赖某个用户的绝对路径。

```powershell
.\scripts\build-windows.ps1
.\scripts\run-windows.ps1
.\scripts\package-portable-windows.ps1 -SkipBuild
```

构建脚本默认执行干净 configure/build、CTest 和 install。便携包生成到
`release/`，包含 ZIP、`SHA256SUMS`、`zip.sha256` 和包内 `manifest.json`。
可选的 Mosaico/Arrow 插件因资源开销默认关闭，可用 `-EnableMosaico` 开启。
当前机器即使串行编译该插件仍会内存不足，因此最终验证包不含 Mosaico。

## 贡献与安全

提交改动前请阅读 [贡献指南](CONTRIBUTING.md)。安全漏洞请按
[安全策略](SECURITY.md) 私密报告，不要在 Issue、fixture 或 CI 日志中提交凭证。
签名密钥和证书私钥不得进入仓库。创建版本 tag 前必须完成本地验收；tag 构建、
测试和复验全部通过后自动公开，并仅保留最新版。

## 许可证

上游 PlotJuggler 源码按 MPL-2.0 分发，见
`src/PlotJuggler/LICENSE.md`。第三方组件继续遵守各自许可证。
