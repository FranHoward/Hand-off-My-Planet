# Windows 随游戏运行环境

游戏运行时不再使用项目 `.venv`、系统 Python、`py` 启动器或开发机目录。
UE 通过 CosmoBridge 插件位置找到 `Resources/Runtime/Win64`，编辑器与打包游戏使用相同目录结构。

## 已部署资源

```text
Plugins/CosmoBridge/Resources/Runtime/Win64/
  python/                    CPython 3.12.10 x64 embedded、标准库、DLL、依赖与许可证
    python.exe
    python312._pth           仅指向本目录中的标准库、依赖和应用
    Lib/site-packages/
  app/
    cosmo_service.py          UE 管理的服务入口与父进程退出监控
    runtime_check.py          离线完整性和推理检查
    CosmoSystem/
      companion/             HTTP、Grok、实时语音、教程逻辑
      data/                  教程配置与提示词
      static/                调试网页
    gestures/
      gesture_recognition.py
      gesture_logic.py
      game_controls.py
      interaction_controls.py
      camera_preview.py
      recognition_settings.json
      gesture_bindings.json
      models/
        gesture_recognizer.task
        face_landmarker.task
        MODEL_SOURCE.txt
  runtime.json               非敏感服务默认配置
  runtime-requirements.lock   20 个固定版本的运行依赖
  manifest.json              文件 SHA-256、Python 来源与依赖版本
```

运行目录约 329 MB。依赖包括 MediaPipe、OpenCV、NumPy、websockets、certifi 及其传递依赖；不包含 pip、pytest、虚拟环境、开发脚本、密钥或摄像头图像。Python 与各依赖附带的许可证保留在包中，模型来源和哈希见 `MODEL_SOURCE.txt`。

Python 来自 [Python 官方 embedded 发行包](https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip)，归档 SHA-256 固定为 `4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3`。解释器通过相对路径 `_pth` 隔离系统 `PYTHONHOME`、`PYTHONPATH` 和用户包目录；启动使用 `-B`，无需向安装目录写入字节码。

## 游戏打包与安装

正常执行 UE 的 Windows 打包流程即可。`CosmoBridge.Build.cs` 将运行目录中的所有文件登记为 **NonUFS** 外部资源，自动复制到打包结果的 `VFX/Plugins/CosmoBridge/Resources/Runtime/Win64`，不塞进 Pak / IoStore；Python 必须能直接访问这些文件。GestureBridge 也会随包复制自己的绑定配置。

分发完整打包目录，不要只复制游戏 exe。不要另外复制 `.venv`、`dubhack_gestures` 或 `External/CosmoSystem`。运行包缺失时 Win64 构建会明确报错，避免生成一个悄悄依赖开发机的版本。UE 的 Windows 系统运行库安装要求继续沿用标准打包配置。

手势启动强制使用 `--require-local-models`；缺少模型时报告安装损坏，不尝试首次联网下载。模型使用内存字节加载，兼容中文和含空格的安装路径。开发源码仍保留手动下载模型功能，游戏启动不会使用它。

## 配置与日志

- `runtime.json`：`provider=auto`、语音模型与音色默认值。显式环境变量优先；auto 模式有密钥时选择 Grok，否则使用 mock。
- Grok 运行需配置 `XAI_API_KEY`、`XAI_MODEL`，并让游戏进程继承。可用 `COSMO_AI_PROVIDER=mock` 强制离线服务。不要把密钥放进分发目录。
- 手势阈值：`app/gestures/recognition_settings.json`；教程及提示词：`app/CosmoSystem/data/`。修改配置或源文件后重新生成运行包，以更新完整性清单。
- 端口沿用 HTTP 8765、实时语音 8766、摄像头预览 8767、手势 UDP 5052。
- 编辑器日志：项目 `Saved/Logs/CosmoService.log` 与 `GestureRecognition.log`。
- 打包游戏日志：当前用户本地应用数据目录的 `COSMO/Logs`，无需安装目录写权限。

## 验证

无需系统 Python：

```powershell
./Plugins/CosmoBridge/Resources/Runtime/Win64/python/python.exe -B ./Plugins/CosmoBridge/Resources/Runtime/Win64/app/runtime_check.py
./Scripts/VerifyRuntimeDeployment.ps1
```

第一项校验全部资源哈希，加载本地手部／面部模型，对合成图像推理，检查 OpenCV JPEG、TLS 根证书、HTTP 健康与游戏事件。第二项将完整运行包复制到含中文和空格的新目录，清除 Python 搜索路径并设置无效的 PYTHONHOME/PYTHONPATH，再验证上述流程及服务退出清理。它不访问 xAI、不采集摄像头或麦克风，日志保存于 `Saved/RuntimeRelocation`。

运行包测试不能代替另一台 Windows 机器上的整游戏安装、摄像头权限和真人语音验收。

## 重新生成运行包（仅开发/构建机）

玩家无需执行这些步骤。构建机使用 CPython **3.12.10 x64**，在专用构建环境中安装锁定依赖：

```powershell
python -m pip install -r Scripts/runtime-requirements.lock
python Scripts/build_runtime.py
```

脚本从当前构建解释器的已安装发行包提取文件，验证版本，保留 DLL、数据和许可证，拒绝可编辑安装／外部路径 `.pth`，不复制虚拟环境。Python embedded 归档会下载到 `Saved/RuntimeBuild`，也可用 `--python-archive <归档路径>` 指定已下载副本；Python 和两份模型均校验固定 SHA-256。

为避免混入旧版本文件，生成器拒绝覆盖已有输出。重建前先将旧的确切目录 `Plugins/CosmoBridge/Resources/Runtime/Win64` 移至 `Saved/RuntimeBuild` 备份，再生成新目录，执行迁移验证并重新构建游戏。`Saved` 下备份和测试副本不会打进游戏。
