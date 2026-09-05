# 项目状态

更新时间：2026-09-05

## 结论

当前版本为 `3.17.5-rc.3`。除 PlotJuggler 3.17.2 上游基线、重新实现的
ROS bag 插件和在线更新外，已从 2026-08-05 至 08-19 的历史补丁恢复 Studio
二次开发 UI：双标尺、测量表、交点标签、智能 linked zoom、自动 fit、品牌化及
MQTT/ZMQ 可靠性修复。当前源码完成增量构建并通过 127/127 CTest。

此前发布的 `3.17.2-studio.1` 缺少这些 UI 接线，已从 GitHub 撤下。发布流程现
要求 tag 构建、测试、打包和复验成功后公开 Release，并在成功后自动删除其他
Release 和旧 `v*` 发布标签，只保留最新版。

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
- 插件先异步检查 bag 索引并显示可过滤、可记忆的 Topic 选择框，再由后台
  `QProcess` 启动 `runtime/rosbag_python/extract_rosbag.py`。worker 在源头过滤
  connection。ROS1 `.bag` 默认输出 `raw-v1` 原始消息字节，由 C++ `ParserROS`
  按 bag 自带 schema 解码；schema 失败的 Topic 回退 `binary-v1`。文本协议仍可
  通过 `RSPJ_FORCE_LEGACY_ROSBAG_PROTOCOL` 强制。数据仅在 worker 正常完成后合并。
- 加载和检查均支持取消且不再用 `processEvents()` 阻塞 UI；标准 MCAP 默认交给
  原生 C++ loader，可通过 `RSPJ_ENABLE_PYTHON_MCAP_FALLBACK` 临时启用 Python
  fallback。
- worker 保留文本协议兼容入口，并新增阶段指标和
  `rosbag-load-performance.log`，便于现场定位打开、反序列化、扁平化、传输和
  合并耗时。
- 新增统一三格式生成器和 worker benchmark。当前机器对 10,000 个时间步、
  8 个数值 Topic、数组宽度 16（共 90,000 条消息、1,610,000 个字段）的生成
  验收结果为：ROS1 4.93 秒、ROS2 SQLite3 4.31 秒、ROS2 MCAP 4.28 秒，worker
  峰值工作集分别为 57.54、44.24、48.45 MiB。该结果验证可重复路径，不替代
  现场真实 bag 和 GUI 合并/重绘验收。
- `3.17.5-rc.3` 将 ROS1 热路径改为原始字节解析。本机 513.78 MiB /
  540,000 条消息的数值包上，`binary-v1` worker 基线 127.16 秒（峰值 RSS
  134.0 MiB）；`raw-v1` worker 8.60 秒（140.4 MiB）。同一文件 GUI 全选加载
  18.44 秒（inspect 1.91s + load 13.60s + merge 1ms），取消退出 0.88 秒。
  端到端约 27.9 MB/s，满足 500MB / 30s 门禁。CI 仍用小 fixture。
- `3.17.5-rc.2` 修复无 Statistics 的 MCAP Topic 被全部禁用、表格排序期间
  Topic/Schema/计数错配及大计数按字符串排序；二进制 decoder 现允许同路径在
  数值/字符串类型之间变化，并拒绝含 NUL 的序列名。worker 对数组、字符串块和
  frame 设置一致上限，重复解码错误只报告一次；文本/二进制控制记录均校验消息
  与字段计数，成功加载但跳过数据时会明确提示。空字符串样本会保留时间戳；
  `.db3`/`.mcap` 存储文件会回溯 ROS2 metadata 目录。原生 MCAP 支持 Windows
  Unicode 路径，扫描未选 Topic 时仍可及时取消，读取中途失败必须由用户明确
  接受部分结果；便携 smoke test 也会检查指定 loader DLL 和二进制 worker。
- `RecordParser` 保持旧构建产物可确认的
  `RecordParser(PlotDataMapRef&)`、`parse(QByteArray)` 和
  `recordCount()` 接口。
- `ruler_metrics.h`、`tracker_label_layout.h` 和 `linked_zoom_policy.h` 不仅已
  恢复测试，还已接入 `CurveTracker`、`PlotWidget` 和 `MainWindow` 运行路径。
- 已恢复 A/B 标尺直接拖动、可见曲线测量表、帧号与差值、选中高亮、标签碰撞
  避让、时间域不兼容时独立 fit，以及加载/重载/布局后的自动 fit。
- `STUDIO_VERSION` 已设置为 `3.17.5-rc.3`，并成为 CMake 的唯一版本源；
  configure 严格校验 SemVer，`PJ_STUDIO_VERSION` 通过 base target 公开给应用和插件。
- Windows CMake 目标和产物均为 `RosPlotJugglerStudio`，插件安装到
  `bin/plugins`，运行时按应用目录相对定位。
- `single_ruler.svg` 和 `reference_line.svg` 已恢复并加入资源清单。

## 发布前阻塞

1. 用现场真实 ROS1 bag、ROS2 SQLite3 bag 和 ROS2 MCAP bag 验证 worker、进度、
   取消、错误提示、字符串及自定义消息行为；未完成前不得发布正式候选包。
2. 最终 GUI 人工验收双标尺拖动、多标签页测量表、隐藏/删除曲线同步和密集标签
   视觉效果；自动化已覆盖计算、布局和采样边界。
3. 在内存更充足的机器上启用 `-EnableMosaico` 构建并验证可选 Mosaico 插件。
4. 发布后用上一版本执行一次真实 GitHub 在线更新和失败回滚验收。

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
- 未执行现场真实 bag 或完整 GUI 运行验证，未将构建/打包等同于功能验收。

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
- CTest：108/108 通过，恢复版干净构建最终复验 2.73 秒。此前未调用 `enable_testing()` 导致 CTest 报告
  “No tests were found”，已通过 `include(CTest)` 修正。
- 安装：通过；确认 `bin/RosPlotJugglerStudio.exe`、`bin/plugins` 下 23 个插件
  DLL 和 `bin/runtime/rosbag_python/extract_rosbag.py`。
- 便携 smoke test：通过；验证可执行文件、插件布局、manifest 和
  `rosbags/lz4/numpy/zstandard` bundled Python imports。
- 恢复版 ZIP：`RosPlotJugglerStudio-3.17.2-studio.2-windows-x86_64.zip`，
  345,534,439 bytes；SHA-256
  `2c1ca579d88ede555a56197d1fde4adb2cc462930ce1bd7637feb1f41329be2d`。
- 打包同时生成 `SHA256SUMS`、同名 `.zip.sha256` 和包内 schema v1
  `manifest.json`；便携树共 12,625 个文件，不含开发头文件。
- 首次干净配置曾因 GitHub 下载 Wasmer 超时失败；脚本现会自动复用仓库根旧
  build 缓存中的本地 CPM 源，若不存在则仍按上游 CMake 下载。
- 未发现可用于运行验证的 `.bag`、`.db3` 或 `.mcap` fixture。
- 阶段 3 增量构建：`RosPlotJugglerStudio` 目标通过（Windows UCRT64，
  `RelWithDebInfo`，串行构建）。
- 阶段 3 单测：新增 `SemVer.*` / `UpdateManifest.*` 共 7/7 通过。
- ROS bag 性能改造增量构建通过；当前全量 CTest 为 119/119，通过项包含二进制
  帧边界/损坏输入测试，以及从生成 ROS1 bag 到 Topic 过滤和 C++ 解码的端到端测试。
- 180,200 条消息、880,200 个输出字段的本地 worker 基准中，旧文本协议中位数
  6.65 秒，二进制协议和路径缓存为 3.78 秒；只选两个数值 Topic 为 1.24 秒。
  该数据不包含旧 GUI 逐行解析开销，不能替代现场 bag 的 GUI 验收。
