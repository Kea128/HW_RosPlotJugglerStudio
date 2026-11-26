# Windows 自动更新

Windows 便携包包含独立的 QtCore/C++17 `RosPlotJugglerUpdater.exe` 和
`extract-update.ps1`。主程序只通过 HTTPS 下载发布资产，并在大小与 SHA-256
校验完成后询问是否“立即安装并重启”。确认后，主程序把 helper、脚本以及
`bin\updater\runtime-dlls.txt` 列出的 Qt/MinGW 运行库复制到当前用户临时目录，
以版本化协议启动 helper，再请求正常关闭。helper 因此不占用被替换版本的 DLL，
成功后能完整删除旧版本。

发布清单严格使用 schema v1，字段为 `schemaVersion`、`version`、`platform`、
`arch`、`channel`、`publishedAt`、`minimumSupportedVersion`、`assetUrl`、
`releaseNotesUrl`、`size` 和 `sha256`。旧 `url` 字段不兼容且会被拒绝。
`package-portable-windows.ps1` 会生成 `update-manifest-stable.json`（或 beta）
以及包内 `manifest.json` marker。

helper 会执行以下检查：

1. 参数无重复或未知字段，所有路径均为规范化绝对路径，安装根包含有效 marker；
2. 同一安装根仅允许一个 updater，父进程必须正常退出；
3. 再次校验 ZIP 的大小与 SHA-256，并检查写权限和可用空间；
4. 删除此前更新在安装根旁遗留的工作目录；
5. PowerShell 使用 `System.IO.Compression` 逐 entry 拒绝绝对路径、路径穿越、
   符号链接和 reparse point，不使用 `Expand-Archive`；
6. staging 必须只有一个预期顶层目录，并包含匹配版本的 marker、主程序、
   helper 和解压脚本；
7. 当前根目录先 rename 为同级 backup，staged root 再 rename 为当前根；安全软件
   短暂占用文件导致 rename 失败时，最多重试 30 秒；
8. 新版本以隐藏的 `--update-health-file` 启动，在 MainWindow 构造且事件循环启动
   后原子写健康文件并退出探针；确认后正常启动新版并删除 backup，失败则恢复
   backup 并重启旧版。

工作目录与安装根同级：`.<安装根名>-new-<8 位十六进制>`（staging）、`-old-`
（backup）和 `-bad-`（回滚时移开的失败版本）；3.17.10 之前的 updater 使用
`-staging-<UUID>`、`-backup-<UUID>` 和 `<安装根名>.failed-<UUID>`，也会被清理。

解压脚本运行在 Windows PowerShell 中，文件路径必须短于 260 个字符、目录路径短于
248 个字符，而整个包都解压在 staging 目录下。打包时检查包内文件路径（含顶层
`RosPlotJugglerStudio\`）不超过 143 个字符、目录路径不超过 131 个字符，因此安装
根（默认名 `RosPlotJugglerStudio`）的上级目录路径不超过 80 个字符时可以在线更新。
3.17.9 及更早版本的 updater 使用 66 个字符的 staging 名，用它们更新到 3.17.10 时
上级目录路径不能超过 58 个字符；例如资源管理器默认解压到“下载”下的
`RosPlotJugglerStudio-<版本>-windows-x86_64` 文件夹时，需要手动下载一次。
打包脚本自行写 ZIP 并使用 `/` 分隔符：Windows PowerShell 5.1 的
`Compress-Archive` 写入 `\`，解压脚本会把这类目录项当作文件；`verify-release.ps1`
会拒绝这样的包。

日志保存在 `%LOCALAPPDATA%\RosPlotJugglerStudio\Updater\updater.log`。更新过程
不读取或覆盖 QSettings 和用户项目。helper 启动后立即持有父进程句柄，父进程在
预检期间已退出也能继续；它不会强制结束用户的主程序，最多等待其正常退出 5 分钟。
健康探针是 helper 自己启动、不含用户数据的新版实例：若超时未确认，或确认后未在
15 秒内退出，helper 会先结束探针，再恢复 backup 并重启旧版。
