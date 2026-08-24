# 更新与 ROS1 回归测试记录

验证日期：2026-08-22  
验证环境：Windows Server 10.0.20348、MSYS2 UCRT64、Qt 5、RelWithDebInfo

## 自动化覆盖

- Manifest / SemVer：严格字段集合与字段类型、缺失和未知字段、SemVer 2.0.0
  官方优先级序列、前导零和溢出、发布时间、最低支持版本、stable/beta、
  `windows/x86_64`、HTTPS、GitHub host allowlist、userinfo、非标准端口、hash
  和整数 size。
- 下载器：通过注入的 Qt fake network manager 验证完整下载提交、截断、SHA-256
  不匹配和取消；失败或取消后目标文件不存在。测试不访问外网。
- updater 参数与预检：协议、绝对规范化路径、重复/未知参数、hash、size、PID、
  timeout、空版本、安装父目录可写探针，以及用“文件而非目录”模拟不可写父目录。
- updater staging：marker 身份、版本、files 数组和三个必需文件。
- ZIP：E2E 动态构造 traversal、absolute 和 Unix symlink external attributes，
  并确认 `extract-update.ps1` 全部拒绝。
- updater E2E：`scripts/test-updater-e2e.ps1` 只删除并重建
  `artifacts/test`。它复制构建出的 updater 和测试 health helper，构造最小真实
  旧版/新版目录及 ZIP，启动短生命周期父进程，验证：
  - 健康新版从 `1.0.0` 切换到 `2.0.0` 并重启新版；
  - 新版健康检查失败时 updater 返回 14、恢复 `1.0.0` 并重启旧版；
  - staging、backup、failed 临时目录清理；
  - 位于安装树之外的配置文件 SHA-256 不变。
- ROS1：`generate_numeric_ros1_sample.py` 生成确定性的 Float64 和 String fixture；
  packaged Python worker 实际读取 bag，确认输出包含 `N`、`S` 和
  `DONE	1005	1005`。`RosbagRecordParser` 的 numeric、string、metadata 和非法
  记录测试均通过。

## 本次执行命令与结果

完整增量构建、CTest 和安装：

```powershell
.\scripts\build-windows.ps1 -Configuration RelWithDebInfo `
  -BuildDirectory .\build\phase2-clean `
  -InstallDirectory .\install\phase2-clean -Jobs 1 -NoClean
```

结果：通过；应用、updater 和全部已启用目标构建并安装。最终完整 CTest 在增加
最后一项 downloader hash 测试后再次执行：

```powershell
.\.toolchain\msys64\ucrt64\bin\ctest.exe `
  --test-dir .\build\phase2-clean --output-on-failure
```

结果：106/106 通过，3.23 秒。DataLoadROSBag parser 单独复验为 4/4 通过。

updater E2E：

```powershell
.\scripts\test-updater-e2e.ps1 -BuildDirectory .\build\phase2-clean
```

结果：通过；没有读取或修改 `install/`、`release/` 中的安装源目录，所有可变
测试数据均位于 `artifacts/test`。

ROS1 fixture 和 worker：

```powershell
$Bin = (Resolve-Path .\release\phase5\portable\RosPlotJugglerStudio\bin).Path
$env:PATH = "$Bin;$env:PATH"
$env:PYTHONPATH = Join-Path $Bin runtime\rosbag_python
& (Join-Path $Bin python.exe) .\rosbag\generate_numeric_ros1_sample.py
& (Join-Path $Bin python.exe) .\runtime\rosbag_python\extract_rosbag.py `
  .\rosbag\ros1\numeric_signals.bag
```

结果：通过；1005 条消息生成 1005 条字段记录，观察到 `N`、`S` 和
`DONE	1005	1005`。

便携打包和 smoke：

```powershell
.\scripts\package-portable-windows.ps1 -Configuration RelWithDebInfo `
  -BuildDirectory .\build\phase2-clean `
  -InstallDirectory .\install\phase2-clean `
  -ReleaseDirectory .\release\phase5 -SkipBuild
```

结果：通过；smoke 确认应用、updater、插件、manifest 和 ROS bag runtime。
生成 ZIP 的 SHA-256 为
`d46e4d749697d0eddc5a2d4092748d6d68733ca1175aad3112292c8815f3d20c`。

## 本次发现并修复

- 健康 helper 很快写 marker 并退出时，`QProcess` 可能已是 `NotRunning`；旧逻辑
  再调用 `waitForFinished()` 会返回 false，从而把健康新版误判为失败并回滚。
  updater 现先检查状态，再等待仍在运行的进程。
- updater 启动 PowerShell 解压器后未关闭其标准输入写通道，在自动化环境中会使
  PowerShell 保持运行。现已在成功启动后关闭写通道；health 子进程也同样处理。
- ROS1 fixture 原先不创建输出目录且只产生 numeric 记录；现可重复创建目录并加入
  String topic，从而实际覆盖 `N`、`S`。

## 仍需人工或外部环境验证

- Help/Preferences 更新 UI、进度对话框、取消按钮、提示文本、正常关闭主程序和
  最终 GUI 重启交互。
- 真实 GitHub HTTPS、TLS、CDN 重定向、代理、离线和大文件网络行为；自动测试
  明确不访问外网。
- Windows 真实 ACL/UAC/杀毒软件、磁盘耗尽、文件被第三方进程占用和断电场景。
  当前权限测试是确定性的不可写父目录模拟，不等同于真实 ACL 验收。
- 安装树内部用户文件的迁移策略。本次仅验证应用实际采用的外置配置不受原子目录
  替换和回滚影响，没有宣称任意放入安装目录的配置会保留。
- 发布签名、安装器升级、真实 345 MB 包 E2E；本次 E2E 使用真实目录结构和 updater，
  但用最小 helper 避免复制完整便携包。
