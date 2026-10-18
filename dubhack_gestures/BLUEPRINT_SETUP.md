# 当前项目蓝图接线

本项目已完成接线，无需添加键盘映射。完整操作见 [GESTURE_ONLY.md](../GESTURE_ONLY.md)。

`BP_FirstPersonCharacterAI` 中的 `GestureJsonReceiver` 事件连接至 `FirstPersonAIIntegration`：

| 接收器事件 | 集成处理 |
|---|---|
| OnAxes | HandleGestureAxes |
| OnActionPressed | HandleGesturePressed |
| OnActionHeld | HandleGestureHeld |
| OnActionReleased | HandleGestureReleased |
| OnConnectionChanged | HandleGestureConnection |

持续动作：MoveForward、MoveBackward、TurnLeft、TurnRight、Shield、Laser。
一次动作：Fire、Wave、Scan、Hint、Repeat、Retry。

动作通过名称分发。KeyName 保留为兼容引脚，值为 None；配置中的 key 为空，不触发键盘。Retry 在死亡或胜利时也可处理，不经过禁止战斗的 CanAct 门禁。

接收器只监听 127.0.0.1:5052。自动识别进程随本地玩家 BeginPlay 启动、EndPlay 结束；命令行自动测试不占用摄像头。

`CodexGestureOnly` 编辑器命令用于移除硬件输入节点、清空旧映射并更新教学标识；`-Verify` 只读审计。实际项目插件在 `Plugins/GestureBridge`，本目录下的 Unreal 子目录为源码副本。
