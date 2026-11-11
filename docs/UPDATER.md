# Windows 自动更新

Windows 便携包包含独立的 QtCore/C++17 `RosPlotJugglerUpdater.exe` 和
`extract-update.ps1`。主程序只通过 HTTPS 下载发布资产，并在大小与 SHA-256
校验完成后询问是否“立即安装并重启”。确认后，主程序把 helper 和脚本复制到
当前用户临时目录，以版本化协议启动 helper，再请求正常关闭。

发布清单严格使用 schema v1，字段为 `schemaVersion`、`version`、`platform`、
`arch`、`channel`、`publishedAt`、`minimumSupportedVersion`、`assetUrl`、
`releaseNotesUrl`、`size` 和 `sha256`。旧 `url` 字段不兼容且会被拒绝。
`package-portable-windows.ps1` 会生成 `update-manifest-stable.json`（或 beta）
以及包内 `manifest.json` marker。

helper 会执行以下检查：

1. 参数无重复或未知字段，所有路径均为规范化绝对路径，安装根包含有效 marker；
2. 同一安装根仅允许一个 updater，父进程必须正常退出；
3. 再次校验 ZIP 的大小与 SHA-256，并检查写权限和可用空间；
4. PowerShell 使用 `System.IO.Compression` 逐 entry 拒绝绝对路径、路径穿越、
   符号链接和 reparse point，不使用 `Expand-Archive`；
5. staging 必须只有一个预期顶层目录，并包含匹配版本的 marker、主程序、
   helper 和解压脚本；
6. 当前根目录先 rename 为同级 backup，staged root 再 rename 为当前根；
7. 新版本以隐藏的 `--update-health-file` 启动，在 MainWindow 构造且事件循环启动
   后原子写健康文件并退出探针；确认后正常启动新版并删除 backup，失败则恢复
   backup 并重启旧版。

日志保存在 `%LOCALAPPDATA%\RosPlotJugglerStudio\Updater\updater.log`。更新过程
不读取或覆盖 QSettings 和用户项目。helper 启动后立即持有父进程句柄，父进程在
预检期间已退出也能继续；它不会强制结束用户的主程序，最多等待其正常退出 5 分钟。
健康探针是 helper 自己启动、不含用户数据的新版实例：若超时未确认，或确认后未在
15 秒内退出，helper 会先结束探针，再恢复 backup 并重启旧版。
