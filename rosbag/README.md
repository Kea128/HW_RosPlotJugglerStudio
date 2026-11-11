# ROS bag fixture 与生成器

本目录保存手工验收用的小型 bag，以及 CTest、smoke test 和 CI benchmark 使用的
确定性生成器。自动化测试在临时目录中现场生成 bag，不依赖这里的二进制文件。

## Fixture

| 路径 | 格式 | 内容 |
| --- | --- | --- |
| `ros1/chatter_50hz.bag` | ROS1 | `chatter`：352 条 `std_msgs/String` |
| `ros1/frost_float64_array.bag` | ROS1 | `/array`（`Float64MultiArray`）与 `/chatter`，各 100 条 |
| `ros1/numeric_signals.bag` | ROS1 | `/vehicle/speed`、`/vehicle/steering_angle` 各 500 条，`/vehicle/mode` 5 条 |
| `ros2/cdr_test/` | ROS2 SQLite3 | `test_msgs` 的 `Arrays` 与 `BasicTypes`，覆盖 bool/整数/浮点数组与嵌套消息 |

`ros1/numeric_signals.bag` 可用 `generate_numeric_ros1_sample.py` 按默认参数重建。

## 生成器

两个脚本都需要 `numpy` 和 `runtime/rosbag_python` 中的 `rosbags`：

```powershell
$env:PYTHONPATH = (Resolve-Path .\runtime\rosbag_python).Path
python .\rosbag\generate_numeric_ros1_sample.py --output .\artifacts\numeric.bag `
  --messages 500 --numeric-topics 2 --string-every 100 --array-width 8
python .\rosbag\generate_benchmark_bags.py --format ros2-mcap `
  --output .\artifacts\benchmark-mcap --messages 10000 --topics 8 --array-width 16
```

`generate_benchmark_bags.py` 的 `--format` 可选 `ros1`、`ros2-sqlite`、`ros2-mcap`；
ROS2 格式输出为包含 `metadata.yaml` 的目录。生成结果放在 `artifacts/` 等被忽略的
目录中，不要提交大型生成 bag。

## 手工检查 worker

```powershell
python -u .\runtime\rosbag_python\extract_rosbag.py .\rosbag\ros1\numeric_signals.bag --inspect
.\scripts\benchmark-rosbag-worker.ps1 -BagPath .\rosbag\ros1\numeric_signals.bag `
  -PythonPath <python.exe> -WorkerPath .\runtime\rosbag_python\extract_rosbag.py `
  -Protocol raw-v1 -Iterations 1
```
