# 维护指南

## 分支与合并

- `main` 应始终可构建。建议启用分支保护：禁止直接 push 和 force-push，要求 PR、
  至少一名非作者批准、对话全部解决、`Windows portable` 必需检查通过，并要求
  分支在合并前为最新状态。
- 限制 tag 创建和 Release 发布权限；发布应由受保护 Environment 的维护者批准。
- PR 不接收仓库写权限或发布 secrets。来自 fork 的代码只在只读 token 下构建。

## 上游同步

当前 PlotJuggler 基线及固定提交记录在 `UPSTREAM.md`。同步上游时：

1. 创建独立同步分支，记录旧/新提交和上游 release notes。
2. 先列出 Studio 修改、ROS 插件和 updater 接口，按子系统小批量合并；不得用
   build/install/release 生成物覆盖源码。
3. 检查 CMake 选项、依赖许可证、资源文件、插件 ABI、网络与更新安全策略。
4. 执行干净 UCRT64 构建、全量 CTest、便携 smoke test、updater E2E、真实
   ROS1/ROS2 bag 和 GUI 插件验收。
5. 更新 `UPSTREAM.md`、功能证据和项目状态，经维护者复核后合并。

## 依赖与生成物

依赖升级应固定来源版本并审查变更，避免在同一 PR 混入无关功能。`build/`、
`install/`、`release/`、`.toolchain/`、`artifacts/` 和测试结果是本地生成物，
不得提交。`runtime/rosbag_python/` 是便携运行时源码/依赖快照，属于必要输入，
不能按普通 Python virtualenv 忽略；升级时需记录版本、许可证并重新验收。

每月检查 GitHub Actions 主版本、MSYS2 包可用性和上游安全公告。第三方 action
只使用固定主版本或完整 commit；高风险发布仓库可进一步固定到经审查的 commit。

## 故障处理

构建或发布失败时保存测试 artifact 和日志，先确认是否为源代码、MSYS2 滚动包、
CPM 下载或 runner 资源问题。不要通过跳过测试、放宽 token 权限或替换已发布资产
规避失败。回滚发布按 [发布流程](RELEASE_PROCESS.md) 执行。
