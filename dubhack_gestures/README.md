# VFX 识别端（纯手势 / 面部交互）

本目录来自本次提供的 Python 压缩包，已按当前 UE 项目完成接入。

完整启动、动作、配置和验证说明见 [GESTURE_ONLY.md](../GESTURE_ONLY.md)。UE Play 会自动启动识别；无需手动使用按键操控识别窗口。

- `gesture_recognition.py`：MediaPipe 手部 / 面部模型、自动校准、UDP 和诊断预览。
- `game_controls.py`、`gesture_logic.py`：保留本次压缩包的握拳停止、捏合激光、面部旋转矩阵转向等识别修正。
- `interaction_controls.py`：增加扫描、提示、重复、重试与双 V 校准，并与战斗共享递增事件 ID。
- `recognition_settings.json`：动作阈值；`gesture_bindings.json`：UE 动作开关、冷却和本机端口，key 必须为空。
- `Unreal/Plugins/GestureBridge`：与项目实际插件一致的源码副本。

手动预览时先关闭 UE 组件的 AutoLaunchRecognition，执行 `python gesture_recognition.py --udp-port 5052`。不带 udp-port 时仅本地识别。
