# 项目状态

更新时间：2026-08-22

## 结论

PlotJuggler 3.17.2 的可确认空白上游文件已按固定提交恢复；此前剩余的
Studio/ROS 定制空白源码也已按现有接口和随包 Python 协议重新实现。阶段 2
已完成 Windows UCRT64 构建、安装和便携打包流水线。当前版本标识为
`3.17.2-studio.1`。更新客户端、外置 updater 的安全替换/健康检查/失败回滚及
本地 E2E 已完成；阶段 6/7 的仓库文件部分已加入 Windows CI、draft Release、
发布复验脚本和维护文档。GitHub 托管 runner 上的首次执行及人工发布验收仍待
完成。这不代表已经完成真实 ROS bag 加载、完整 GUI 或发布验收。

## 基线证据

- 固定上游提交：
  `034a5cc919909aebb124e0dc1abbe2029c3eed0f`。
- 恢复了 34 个错误的 0 字节上游文件，并恢复了 4 个实际仅含换行的上游资源。
- 38 个恢复文件的 Git blob SHA-1 均与固定提交 tree 一致。
- 固定提交中本来为空的 `.gitmodules` 和 rosbag2 WAL fixture 保持原样。
- 旧 `CMakeCache.txt` 记录：
  - PlotJuggler 3.17.2；
  - Windows / Ninja / MinGW UCRT64；
  - C++17、Qt 5；
  - `RelWithDebInfo`；
  - 安装前缀为工作区 `install/`；
  - Python 3.14 development 库曾被发现；
  - 非 ROS 配置。
- `build/` 中存在配置和生成记录，`install/` 中存在 CMake package 与图标，
  但未发现可作为当前源码成功构建证据的 PlotJuggler 可执行文件。
- 恢复后重新配置成功；针对恢复目标的编译和纯逻辑单元测试实际执行成功。

## 本次恢复的 Studio/ROS 源码

- `DataLoadROSBag` 重新建立插件、解析库、测试目标及 Python worker 打包规则。
- 插件通过 `QProcess` 启动
  `runtime/rosbag_python/extract_rosbag.py`，处理 `N`、`S`、`INFO`、
  `PROGRESS`、`DONE`、`ERROR` 和 `WARN` 记录；数据先写入临时
  `PlotDataMapRef`，仅在 worker 正常完成后合并。
- `RecordParser` 保持旧构建产物可确认的
  `RecordParser(PlotDataMapRef&)`、`parse(QByteArray)` 和
  `recordCount()` 接口。
- `ruler_metrics.h`、`tracker_label_layout.h` 和对应测试已恢复；
  `linked_zoom_policy.h` 与这些测试已接入 app CMake。
- `STUDIO_VERSION` 已设置为 `3.17.2-studio.1`，并成为 CMake 的唯一版本源；
  configure 严格校验 SemVer，`PJ_STUDIO_VERSION` 通过 base target 公开给应用和插件。
- Windows CMake 目标和产物均为 `RosPlotJugglerStudio`，插件安装到
  `bin/plugins`，运行时按应用目录相对定位。
- `single_ruler.svg` 当前未被 `resource.qrc` 或 app 源码引用，因此没有把空文件
  加入资源清单，也无需从清单删除条目。

## 发布前阻塞

1. 用真实 ROS1 bag、ROS2 SQLite3 bag 和 ROS2 MCAP bag 验证 worker、进度、
   取消、错误提示、字符串及自定义消息行为。
2. 用 GUI 验证安装树中所有插件的实际加载和交互；便携 smoke test 已验证目录、
   插件 DLL 存在、应用 `--version` 启动及 Python runtime imports。
3. 在内存更充足的机器上启用 `-EnableMosaico` 构建并验证可选 Mosaico 插件。
4. 启动后验证主题、布局、加载器、streamer、transform 和 Studio 定制，再更新
   `FEATURE_PARITY.md` 的验证标记。
5. 在 GitHub 托管 Windows runner 首次执行新增 workflow，确认 MSYS2 滚动包名、
   CPM 网络下载、artifact 大小/配额和 draft Release 权限符合预期。
6. 按 `docs/RELEASE_PROCESS.md` 在独立 Windows 机器完成 draft 资产人工验收后，
   才能 publish；当前没有已人工验收的公开版本。

## 阶段 3：更新客户端

- `plotjuggler_app/studio/update` 已加入严格 schema v1 manifest 解析、
  SemVer 2.0.0 比较、平台/架构/通道匹配，以及发布时间、最低支持版本、大小、
  SHA-256 和 HTTPS GitHub host allowlist 校验。
- checker 使用 `QNetworkAccessManager` 异步请求，设置 User-Agent、15 秒超时、
  最多 5 次受控重定向；stable/beta 默认地址指向项目 GitHub release，可分别用
  `PJ_STUDIO_UPDATE_MANIFEST_STABLE_URL` 和
  `PJ_STUDIO_UPDATE_MANIFEST_BETA_URL` 覆盖以便测试。
- downloader 以 `QSaveFile` 流式写入临时文件，支持非阻塞进度和取消，完成前同时
  校验声明大小和流式 SHA-256；重定向仍强制 HTTPS 与 GitHub allowlist。
- coordinator 提供 Idle/Checking/UpdateAvailable/Downloading/Ready/Error 状态机
  和外置 updater launcher 接口。下载完成会明确提示“已校验但尚未安装”，不会
  自动或强制升级。
- Help 菜单已增加 “Check for Updates...”；Preferences 已增加默认启用的启动
  自动检查和 stable（默认）/beta 通道；About 明确显示 `PJ_STUDIO_VERSION`。
- 移除了旧 GitHub release 检查及其 TLS 绕过；网络请求不再关闭证书校验。
- 新增 7 个 UpdateManifest/SemVer 测试。checker/downloader 的 fake reply
  单元测试因 Qt5 `QNetworkReply` 注入成本较高，留作后续本地 HTTP(S) 集成测试。

## 本阶段未做

- 未用对象文件、生成 UI、安装目录或发布二进制反向替代源码。
- 未宣称旧 `release/` 产物与当前源码一致。
- 未修改计划文件，未创建 Git 提交。
- 未执行真实 bag 或完整 GUI 运行验证，未将构建/打包等同于功能验收。

## 阶段 6/7：CI 与维护文件

- 新增 `.github/workflows/windows-portable.yml`：PR/`main` 使用只读权限执行
  MSYS2 UCRT64 构建与测试；`v*` tag 必须严格等于 `v<STUDIO_VERSION>` 才会
  打包、两次复验并以独立 `contents: write` job 创建 draft Release。
- CI 明确安装 UCRT64 构建、Qt5、测试和 Python ROS bag runtime 依赖，默认关闭
  Mosaico；保存测试结果，并上传 ZIP、SHA-256 和 update manifest。
- 新增 `scripts/verify-release.ps1`，检查 tag/版本、manifest URL、大小、SHA-256、
  release notes URL，以及 ZIP 内 marker 和入口文件。
- 新增发布、维护、贡献和安全文档，明确 stable/beta、draft 到人工验收再 publish、
  上游同步、发布回滚、分支保护和签名密钥不入库。
- 根 `.gitignore` 已覆盖本地构建、发布、测试、归档和常见凭证/签名材料；保留
  `runtime/rosbag_python` 和上游测试资源作为必要源码/运行时输入。
- 这些是仓库文件与本地语法/产物复验结果；CI 尚未在 GitHub runner 实际执行，
  不能宣称远端流水线已经通过。

## 本阶段检查结果

- 干净 CMake configure：通过，使用仓库内 MSYS2 UCRT64、本地缓存依赖和
  `RelWithDebInfo`。
- 全量目标（除显式关闭的可选 Mosaico）：构建通过；首次 8/2 并发和后续
  Mosaico 串行编译均因系统内存不足，最终以串行和
  `PJ_BUILD_MOSAICO_PLUGIN=OFF` 完成。
- CTest：86/86 通过，最终复验 3.47 秒。此前未调用 `enable_testing()` 导致 CTest 报告
  “No tests were found”，已通过 `include(CTest)` 修正。
- 安装：通过；确认 `bin/RosPlotJugglerStudio.exe`、`bin/plugins` 下 23 个插件
  DLL 和 `bin/runtime/rosbag_python/extract_rosbag.py`。
- 便携 smoke test：通过；验证可执行文件、插件布局、manifest 和
  `rosbags/lz4/numpy/zstandard` bundled Python imports。
- ZIP：`RosPlotJugglerStudio-3.17.2-studio.1-windows-x86_64.zip`，
  344,011,577 bytes；SHA-256
  `a582e8cf68964f9f75ead25646e3f43919a710cce815ea529569fb24c2c403d2`。
- 打包同时生成 `SHA256SUMS`、同名 `.zip.sha256` 和包内 schema v1
  `manifest.json`；便携树共 12,625 个文件，不含开发头文件。
- 首次干净配置曾因 GitHub 下载 Wasmer 超时失败；脚本现会自动复用仓库根旧
  build 缓存中的本地 CPM 源，若不存在则仍按上游 CMake 下载。
- 未发现可用于运行验证的 `.bag`、`.db3` 或 `.mcap` fixture。
- 阶段 3 增量构建：`RosPlotJugglerStudio` 目标通过（Windows UCRT64，
  `RelWithDebInfo`，串行构建）。
- 阶段 3 单测：新增 `SemVer.*` / `UpdateManifest.*` 共 7/7 通过。
