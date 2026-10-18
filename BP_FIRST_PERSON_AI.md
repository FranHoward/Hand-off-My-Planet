> 输入系统已更新为纯手势 / 面部控制。本文旧按键、文字聊天和后备输入说明已失效；当前动作与启动方式见 [GESTURE_ONLY.md](GESTURE_ONLY.md)。

# BP_FirstPersonCharacterAI 功能与运行

实际资产：`Content/Blueprint/BP_FirstPersonCharacterAI.uasset`。项目里拼写是 **Character**，不是 Characater。
默认 `BP_FirstPersonGameMode` 已使用该角色。原键鼠、手柄、触屏移动保持可用。
蓝图事件图连接输入和服务事件；`FirstPersonAIIntegration` 组件实现技能、教程、界面和 HTTP 协调逻辑。

## 启动和控制

项目根目录运行 `./Scripts/StartCosmo.ps1`，打开 `VFX.uproject`，在默认 `Lvl_FirstPerson` 关卡 Play。
手势模式另启动 `dubhack_gestures/gesture_recognition.py`；没有摄像头时可用下表键鼠入口。
HUD 显示 COSMO 连接、provider、手势连接、教学阶段、生命值、持续技能和字幕。

| 功能 | 键鼠入口 | 手势 / 蓝图入口 |
|---|---|---|
| 移动 / 视角 | WASD / 鼠标 | 左手前推或后拉 / 头部左右转向 |
| Shield | 按住 LeftShift，松开关闭 | 右手张掌持续；Pressed / Released |
| Fire | F | 右手握拳后前推；每个事件只发射一次 |
| Wave | Q | 右手水平挥动；每个事件只施放一次 |
| Laser | 按住鼠标左键 | 右手拇指、食指持续捏合，松开停止；捏合中心映射视口 |
| 扫描 | E，保持目标可见 | `ScanEnemy` |
| 教学提示 / 重复 | H / R | `RequestHint` / `RepeatInstruction` |
| 问答 | Enter 打开输入框，再 Enter 发送；Escape 关闭 | `SubmitVoiceTranscript(Text)` |
| 语音输入状态 | STT 开始 / 结束时调用 | `SetVoiceListening(bool)` |
| 危险 / 解除危险 | 游戏逻辑调用 | `SetDanger(bool)` |

聊天、死亡、失去窗口焦点会释放持续技能。摄像头超过配置的 400ms 未收到有效帧时，接收器自动释放护盾/激光并停止轴输入。
键盘与手势使用同一技能实现，Fire/Wave 不会因 Held 每帧生成；JSON 去重由接收器负责。

## 已实现的行为

- Shield：可见护盾、持续/释放、阻挡本角色生命值伤害。
- Fire：Niagara 火球、实际弹体移动、碰撞单次伤害、冷却和自动销毁。
- Wave：SoundWave Niagara 特效、前半球范围伤害、遮挡检查、按 Actor 去重、击退/物理冲量。
- Laser：持续光束、手势指向/鼠标准星、射线命中、按 DeltaTime 计算伤害、释放与断线关闭。
- 移动确认：本阶段内在地面实际累计行走；空输入、撞墙、瞬移不会完成。
- 扫描确认：命中目标并保持可见达到扫描时间；打断后重新扫描。
- Boss：对真实 Boss Actor 做距离和可见性检查，在正确阶段确认遭遇。
- 九阶段教学：严格按服务端 expected_event 推进；只有技能真正成功后报告；语音/文字不完成阶段。
- 受伤/死亡：生命扣除、护盾阻挡、危险播报限频、低血量危险状态、死亡停止移动和技能。
- COSMO：按序 HTTP、重连保留状态、实时游戏事实、提示、重复、问答、字幕、本地 TTS 和五种动画事件。
- AI 上下文：生命值、护盾当前状态、技能冷却可用性、扫描目标、Boss 目标及其可读取的 `Health`；遥测不推进教程。

新默认关卡已完成普通难度配置，见 [外星行星关卡与数值](ALIEN_PLANET_LEVEL.md)。该关卡由 `DA_NormalBalance` 统一应用玩家、敌人和补给数值。下面的组件数值是未加载关卡 Director 时的原型默认值。
在角色蓝图的 `FirstPersonAIIntegration` 组件 Details 中调整：Fire 25伤害/0.5s冷却，Wave 15伤害/1s冷却/600cm半径，Laser 20伤害每秒/6000cm射程，移动确认100cm，扫描1s/3000cm，Boss遭遇1500cm。

## 关卡与动画资源

扫描对象需有 `Enemy` 或 `Boss` Actor Tag，或是非玩家 Pawn。Boss 对象需有 `Boss` Tag。
默认关卡现为 `/Game/AlienPlanet/Lvl_AlienPlanet`，已布置 5 只带真实追击/攻击/受伤/死亡的小怪、1 只动画 Boss 和水晶眼扫描对象；普通难度、训练靶、补给、检查点重试及星核通关由 `CosmoPlanetDirector` 管理。
新增其他敌人/Boss 时需设置 Tag、碰撞和伤害处理。遭遇逻辑也可调用 `ConfirmBossEncounter(Boss)`，它仍检查距离、视线和阶段。
原型的最后教学阶段默认自动确认；新关卡会关闭 `AutoCompleteFinalStage`，在 Boss 击杀、星核充能完成时由真实关卡目标调用 `CompleteTutorial()`。

Grok 的形象为 Crystal Eye：`BP_AICompanionManager` 的 `CompanionMesh` 使用
`/Game/Creatures/CrystalEye/Mesh/SK_CrystalEye`、原始材质和 `ABP_CrystalEye`。
玩家的 `AICompanionManager` Child Actor 组件挂在 Capsule 下，相对位置为 `(160, 100, 20)` cm，
模型缩放为 `0.30`、相对 Yaw 为 `-90°`，位于玩家右前方；移动、转向和传送均保持相对位置。
模型关闭碰撞与重叠事件。ABP 读取父玩家速度，静止播放 Idle，移动播放 FloatForward。
连接时自动将 Integration 的 `CompanionMesh` 指向该模型，并复用玩家现有 Grok 客户端。

Crystal Eye 原始动画只有 Idle、FloatForward 和 Controlled。语音、扫描、庆祝与警告事件继续通过
`OnCompanionAnimationRequested` 提供；需要专用动作时可设置 `CompanionMontages`，按
`talk_gesture`、`listen_lean_in`、`scan_pulse`、`celebrate`、`warn_point` 映射 Montage。

离线验证：`UnrealEditor-Cmd.exe VFX.uproject -run=CodexCompleteCharacter -VerifyCompanion -EnablePlugins=CodexBlueprintTools -unattended -nullrhi`。
20 项检查覆盖重新加载后的模型/ABP/材质、子物体关系、跟随位移/旋转/传送、实际骨骼播放和 Idle/Move 切换，
结果见 `Saved/Logs/CrystalEyeCompanion_Verify.log`。修改前备份位于 `Saved/CodexBackups/CrystalEyeCompanion_20261018_030919`。

## 真实 Grok、声音与麦克风

默认 `COSMO_AI_PROVIDER=local`，HUD 显示 `local`，运行本地确定性教学。
真实 Grok 使用 [xAI 官方 Responses/function calling 接口](https://docs.x.ai/developers/tools/function-calling)，与官网 `grok-4.7` 示例一致。
在**服务端**配置后启动：

```powershell
$env:COSMO_AI_PROVIDER = 'xai'
$env:XAI_API_KEY = '<服务端密钥>'
$env:XAI_MODEL = '<账号可用的模型 ID>'
./Scripts/StartCosmo.ps1
```

密钥不写入蓝图、资产或配置文件。模型仅有四个只读教学/游戏状态工具，不能施法或完成阶段。
模型失败时回退本地教学并显示提示。本次没有 API Key，未执行真实付费模型调用。
本地 TTS 使用 UE TextToSpeech 插件，无音频设备时保留字幕；这不是 Grok Realtime Voice，默认本地音色主要支持英文。
原项目没有麦克风采集/STT服务。将识别服务的**最终文本**交给 `SubmitVoiceTranscript`，开始/结束时调用 `SetVoiceListening`。
Enter 文字问答可完整验证同一 AI 回复通路。

## 验证和维护

运行 `./Scripts/VerifyCharacterAI.ps1`，已编译时可加 `-SkipBuild`，用 `-EngineRoot` 指定 UE 安装位置。
脚本在独立端口18765启动临时服务并在结束后关闭，不使用真实模型或 API 额度。
验证包括实际蓝图、技能伤害、完整教学、JSON去重、Pointer、超时释放、受伤、死亡、字幕、服务和手势测试。

蓝图重建命令：`UnrealEditor-Cmd.exe VFX.uproject -run=CodexCompleteCharacter -EnablePlugins=CodexBlueprintTools -unattended -nullrhi`。
加 `-Inspect` 只检查现有资产。旧 `CodexWireInput` 会覆盖技能入口，以后应使用 `CodexCompleteCharacter`。
原蓝图和 GameMode 备份在 `Saved/CodexBackups/CharacterAI_FullIntegration`。

最近一次 UE 接入验证：Unreal 构建成功、47 项运行检查通过。后续 Responses API 适配后，Python 服务全套 83 项测试通过。
默认关卡已在1280×720下实际渲染检查，预览位于 `Saved/Screenshots/CharacterAI_Preview.png`。
运行验证日志为 `Saved/Logs/CharacterAI_Verification.log` 与 `Saved/Logs/CharacterAI_RuntimeTest.log`。
