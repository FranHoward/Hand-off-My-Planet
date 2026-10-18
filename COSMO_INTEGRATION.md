## 自动启动与关闭（当前行为）

打开 UE 并 Play 后，CosmoClient 自动检查默认本机服务的 /health。已有兼容 COSMO 服务时直接复用；未运行时使用插件资源中的独立 Python 启动随包 app/cosmo_service.py，无需先手动启动。启动期间游戏请求保持顺序排队，健康检查通过后发送。单次探测限时 1 秒，总启动预算 20 秒；运行文件缺失、端口返回不兼容服务、进程退出或超时会显示错误。编辑器日志位于 Saved/Logs/CosmoService.log；打包游戏写入用户本地应用数据目录 COSMO/Logs。

停止 Play / 退出游戏时取消探测和请求，并关闭本次启动的 COSMO 进程；手动启动的已有服务继续运行。UE 异常退出时，服务的父进程监控也会结束服务。手势识别继续使用原有自动启动与退出释放流程。

服务继承启动 UE 时的环境变量：显式 COSMO_AI_PROVIDER 优先；未指定时，有 XAI_API_KEY 自动选 Grok，无密钥使用本地 mock。Grok 还需 XAI_MODEL；语音沿用 XAI_VOICE_MODEL / XAI_VOICE，否则使用已有默认值。在配置好环境的同一终端启动 UE；已经打开的 UE 无法继承后来在其他终端设置的变量。密钥不写入项目或启动参数。健康检查通过仅证明本机服务就绪，真实 Grok 可用性仍以实际回复为准。

CosmoClient.AutoLaunchService 可关闭自动管理。自动启动适用于默认 http://127.0.0.1:8765 和交互式游戏；自定义地址、无人值守及 commandlet 测试继续使用外部服务。当前已提供完整 Win64 随包运行环境，UE 打包自动复制 Python、依赖、模型和配置，详见 [运行环境部署](RUNTIME_DEPLOYMENT.md)。

验证：UE Editor 编译通过；Python 187 项测试及 6 项子测试通过；角色集成 59 项通过；Scripts/test_cosmo_lifecycle_smoke.py 验证本地 mock 启动、健康就绪与父进程退出清理。尚未进行此次自动启动后的真人语音验收。

以下手动启动方式保留用于独立调试。
> 输入系统已更新为纯手势 / 面部控制。本文旧按键、文字聊天和后备输入说明已失效；当前动作与启动方式见 [GESTURE_ONLY.md](GESTURE_ONLY.md)。

# COSMO AI 接入

角色蓝图已完成接线与运行实现，详见 [BP_FIRST_PERSON_AI.md](BP_FIRST_PERSON_AI.md)。

项目根目录运行 `./Scripts/StartCosmo.ps1`，服务默认监听 `127.0.0.1:8765`。
打开 UE 默认 `Lvl_AlienPlanet` 关卡 Play，GameMode 已生成 `BP_FirstPersonCharacterAI`。
手势接收使用 `127.0.0.1:5052`；当前游戏采用纯手势和面部控制。

## 接口

- `POST /events`：实际玩法确认。顺序为 `game_started` → `movement_completed` → `shield_activated` → `fire_activated` → `wave_activated` → `laser_activated` → `enemy_scanned` → `boss_encountered` → `tutorial_completed`。
- `POST /game/state`：实时同步生命、技能可用性、目标等事实，不能完成教学或修改施法确认历史。
- `POST /player/utterance`：文字或 STT 最终文本，只回答问题，不推进教学。
- `GET /snapshot`、`/tutorial/status`、`/tutorial/step`、`/tutorial/hint`：状态、指令与提示。
- 伤害/危险事件：`player_damaged`、`player_in_danger`、`danger_cleared`。
- 蓝图已接入连接、响应、字幕、本地 TTS 和 speaking/listening/scanning/celebrating/warning 动画事件。
- 默认使用本地 guide；服务端 `COSMO_AI_PROVIDER=xai`、`XAI_API_KEY`、`XAI_MODEL` 可启用真实 Grok 通路。

## Grok 接入与来源验收

复用项目根目录已有的 Python 3.12 `.venv`，依赖见 `External/CosmoSystem/requirements.txt`。
服务目录是 `External/CosmoSystem`，当前项目版本为 `0.2.0`。
真实问答使用 xAI 官方 `POST /v1/responses`，与官网 `grok-4.7` 示例一致。
工具调用通过 `function_call` / `function_call_output` 交互，只提供已注册的四个状态/教程工具。
同一次问答的工具循环会回传响应中的加密推理项；字幕只读取最终 `output_text`，未完成或错误响应会回退本地。
已收到 `External/cosmo-system.zip`，其中不含 `README_zh-CN.txt`、`apply_patch.py` 或 `test_xai.py`。
因此未执行所述补丁脚本；已检查包内 `0.2.0` 版本并按本次接入要求手动适配现有代码。
原压缩包保留，逐文件 SHA-256 对比见 `Saved/Logs/GrokIntegration_ArchiveComparison.json`。
现有文件修改前已备份到 `Saved/CodexBackups/GrokIntegration_20261018_004210`，包含原文件 SHA-256 清单。

先运行 mock：

```powershell
./Scripts/StartCosmo.ps1 -Mode mock
Invoke-RestMethod http://127.0.0.1:8765/health
```

在 Python 服务所在的 PowerShell 中设置 `XAI_API_KEY` 和账号可用的 `XAI_MODEL` 环境变量。
密钥只放在服务端环境，不要填入 UE 蓝图、项目配置、聊天或命令行参数。
可以用以下方式输入密钥，避免把密钥本身保存在 PowerShell 命令历史中：

```powershell
$xaiSecret = Read-Host 'xAI API Key' -AsSecureString
$env:XAI_API_KEY = [System.Net.NetworkCredential]::new('', $xaiSecret).Password
Remove-Variable xaiSecret
# 先用下面的 --list-models 查询，再把可用的模型 ID 写入 XAI_MODEL。
$env:XAI_MODEL = '<账号可用的模型 ID>'
```

然后执行（以下命令从项目根目录运行）：

```powershell
# 只列出当前账号的可见模型，不生成回复。
./.venv/Scripts/python.exe ./External/CosmoSystem/test_xai.py --list-models
# 如果上一步列表包含 grok-4.7，可选择该模型。
$env:XAI_MODEL = 'grok-4.7'
# XAI_MODEL 配好后，此步骤会进行一次真实模型问答，可能涉及多轮只读工具调用。
./.venv/Scripts/python.exe ./External/CosmoSystem/test_xai.py
```

若 `--list-models` 返回 HTTP 400，先查看脚本显示的 `Server detail (credentials redacted)`。
这一步是 `GET /v1/models`，不使用 `XAI_MODEL`，也不会生成模型回复。
脚本会在发送前检查密钥是否误带引号、`Bearer`、空格或换行；服务器错误中的密钥会被替换为 `[REDACTED]`。
需要排查时，在刚才配置密钥的同一个 PowerShell 窗口重新运行，将脱敏后的错误信息提供出来即可。
该诊断显示和密钥遮蔽已通过 7 项离线检查；真实错误原因仍以服务器返回为准。

关闭占用 8765 的 mock 服务，再启动：

```powershell
./Scripts/StartCosmo.ps1 -Mode grok
Invoke-RestMethod http://127.0.0.1:8765/player/utterance -Method Post `
  -ContentType 'application/json; charset=utf-8' -Body '{"text":"Which combat abilities can I use?"}'
```

只有 `/player/utterance` 的回复带 `source: "grok"` 才代表收到真实模型内容。
mock 返回 `source: "mock"`；上游断网/超时等失败后本地回复返回 `source: "fallback"` 和 `provider_error`。
`/health` 的 `ok` 及 `provider: "xai"` 只说明本地服务及所选配置，不能证明上游已连通。
UE 的 `FCosmoResponse.Source`、`GuideReplySource` 和 HUD 的 `reply` 同样显示实际回复来源，后续健康检查不会覆盖这个标记。
角色蓝图已有 30 秒请求超时；新建客户端的默认值也已统一为 30 秒，大于 Python 的 20 秒模型请求预算。

Windows 接入验收跑通 47 项 UE5 自动运行检查，当时 Python 全套通过 100 项测试及 6 项子测试。
随后 Responses API 适配完成，当前 Python 服务全套 83 项测试通过，日志见 `Saved/Logs/XaiResponses_Test.log`。
UE 自动检查使用无画面、无音频设备的测试模式，不能代替现场听音或动画观感验收。
已有 `BP_FirstPersonCharacterAI` 的实际技能成功事件、HTTP 完成回调、字幕、TextToSpeech 及动画委托已接线。
`HttpBlueprint` 和 `JsonBlueprintUtilities` 已在项目中显式启用。
已创建 `/Game/Blueprint/BP_AICompanionManager`，作为 `BP_FirstPersonCharacterAI` 的 Child Actor 组件 `AICompanionManager` 自动生成。
它继承 `AAICompanionManager`，直接复用玩家的 `CosmoClient` 和 `FirstPersonAIIntegration`，不创建第二套会话、技能或朗读逻辑。
外部输入可调用 `SubmitPlayerQuestion(Text)`；完成回调使用 `OnResponse`，字幕使用 `OnSpokenLine`，动画使用 `OnAnimation`。
`LastResponse`、`Subtitle`、`ReplySource`、`AnimationEvent` 可直接供蓝图读取。
技能成功事件已由现有玩法逻辑上报，绑定伙伴时不要再对同一次成功重复上报。
伙伴 AnimBP 可从 Manager 的 `AnimationEvent.State` 读取 speaking/listening/scanning/celebrating/warning。
玩家语音监听开始/结束时调用角色 Integration 的 `SetVoiceListening(bool)`；Manager 的 `OnAnimation` 同时转发该本地监听状态和服务回复动画。
单独重建 Manager 资产可运行 `UnrealEditor-Cmd.exe VFX.uproject -run=CodexCompleteCharacter -CompanionManager -EnablePlugins=CodexBlueprintTools -unattended -nullrhi`。
验证脚本已修正为构建 `VFXEditor`，并使用 UBT 的 `-EnablePlugin=CodexBlueprintTools` 确保验证工具与运行插件同步编译。
伙伴专用 SkeletalMesh / AnimBP / Montage 仍未提供；现有 `CompanionMesh`、`CompanionMontages` 和
`OnCompanionAnimationRequested` 是待映射入口，不代表已完成伙伴动画资源接入。
真实 API 问答已由用户在本机运行 `test_xai.py` 验收：`source: "grok"`、`model: "grok-4.7"`，
回复正确列出 Shield / Fire / Wave / Laser，`advanced: false`。该用户提供的结果记录在 `Saved/Logs/Grok_RealApi_UserAcceptance.json`。
Grok 原生实时语音和 UE 麦克风采集现已接入，启动和验收见下节；现场听音仍需真人确认。
密钥仅存在于用户已配置的 PowerShell 进程环境中，因此需在该窗口启动 `./Scripts/StartCosmo.ps1 -Mode grok -Model grok-4.7`。

## UE 实时语音与右上角摄像头

在已经配置 `XAI_API_KEY` 的 PowerShell 窗口中停止旧服务，再运行：

```powershell
.\Scripts\StartCosmo.ps1 -Mode grok -Model grok-4.7 -VoiceModel grok-voice-latest -Voice eve
```

服务保持运行，打开 UE 点击 Play。UE 自动启动同一份手势/面部识别程序；右上角显示镜像摄像头、手势标注、语音状态和玩家转写，COSMO 字幕显示在底部。摄像头保留原比例，断开后清除旧画面。无需额外打开浏览器或识别窗口。

等待 `Listening - speak to COSMO` 后直接说话，例如“COSMO，我现在应该做什么？”。服务端自动判断说话结束，返回 Grok 原生声音和字幕。默认在 COSMO 播放期间暂停麦克风采集，避免扬声器回声成为新的问题；COSMO 说完后恢复收音。若戴耳机并需要插话，可在角色 `FirstPersonAIIntegration` 上开启 `AllowVoiceInterruptions`。

同一组件的 `VoiceVolume`（0–1）和 `VoicePitch`（0.5–2，默认 1）控制 UE 播放音量和音高。修改蓝图默认值后重新 Play；也可通过运行时 `Realtime` 组件调节。`SetMicrophoneMuted(bool)` 可供蓝图静音。切换音色用启动参数 `-Voice`，例如 `eve`；它和播放音高分别控制音色与音调。

若没有麦克风状态，检查 Windows“隐私和安全性 → 麦克风 → 允许桌面应用访问麦克风”，以及系统默认输入设备。`Grok Voice unavailable` 后显示的是服务端脱敏错误；`Voice offline` 表示需要检查 Python 服务。断线自动重连；语音服务不可用时，游戏真实事件、字幕和已有本地 TTS 仍可工作。停止 Play 会释放 UE 麦克风、语音连接、音频队列，并结束该局启动的摄像头进程。

| 通路 | 地址 | 内容 |
|---|---|---|
| 原有游戏 HTTP | `http://127.0.0.1:8765` | 真实玩法事件、状态与文字问答 |
| UE → Python → Grok Voice | `ws://127.0.0.1:8766` | 24 kHz、单声道 PCM16 音频、转写与字幕 |
| 识别程序 → UE 预览 | `http://127.0.0.1:8767/frame.jpg` | 约 10 FPS、最大宽 480 的内存 JPEG |

长效 API Key 始终由 Python 持有。摄像头复用识别程序的同一个采集设备，仅在本机传给 UE，不保存图片、不传给 Grok；麦克风音频通过 Python 转给 Grok。原有浏览器测试页的系统朗读设置与 UE 原生语音设置分开。

语音只开放现有四个状态/教程查询工具，与 HTTP 共用同一份权威教程状态。玩家说“完成了”不会调用 `/events`、激活技能或推进教程；真实技能成功继续走现有事件链。实时字幕通过 Integration 和 `BP_AICompanionManager.OnSpokenLine` 转发，`ReplySource` 为 `grok`；它不会再触发第二次本地 TTS。语音事件不伪造 HTTP `OnResponse`，HTTP 完成回调仍用于原有请求。

已对照 [xAI 实时语音说明](https://docs.x.ai/developers/model-capabilities/audio/speech-to-speech) 和 [用户提供的 Voice Agent 示例](https://docs.x.ai/cookbook/examples/voice-agent-web) 接入。语音模型使用 `XAI_VOICE_MODEL`，默认 `grok-voice-latest`，与文字问答的 `XAI_MODEL=grok-4.7` 分开配置。`/health.realtime.status=listening` 仅表示本机端口已监听；完成真实音频回复才证明上游可用。

无需录音即可复查真实服务：

```powershell
.\.venv\Scripts\python.exe .\External\CosmoSystem\test_xai_voice.py
```

它通过正在运行的本机服务发起一次真实语音问答，会产生 API 用量，输出 `source`、模型、音色、字幕和 `audio_bytes`，不会保存音频。2026-10-18 已收到 `source: "grok"`、`model: "grok-voice-latest"`、`voice: "eve"` 的完整声音与字幕。进一步通过 UE 原生 WebSocket 收到真实音频、更新字幕、转发伙伴蓝图回调，15 项在线 UE 验证全部通过；日志为 `Saved/Logs/RealtimeAV_LiveGrok.log`。此验证未录制麦克风，也不代替真人听音。

当前 Python 全套 187 项测试、6 项子测试通过。离线 UE 回归包括 27 项音视频检查、59 项角色验证和 68 项关卡验证。`Scripts/VerifyRealtimeAV.ps1` 使用独立的 18766/18767 端口和合成音频/画面；测试中的 `source:grok` 只验证解析，不是上游接通证据。真实摄像头已经过手部/面部识别后成功提供 480×360 预览，并正常退出；检查未保存真实图像。1280×720、888×500 和 1024×768 布局使用合成摄像头画面验证。4:3 下原有关卡相机保持画面比例，HUD 位于游戏有效画面内。

本次修改前备份：`Saved/CodexBackups/RealtimeAV_20261018_021441`。UE runtime 实现在 `CosmoRealtimeComponent`，Python 转发在 `companion/realtime_voice.py`，预览供给在 `dubhack_gestures/camera_preview.py`。

## 教程优先与地点台词

教程与地点预设台词高于实时对话。触发后立即清空当前闲聊音频，取消其生成并暂停玩家收音；字幕固定显示预设原文，迟到的闲聊字幕和音频不能覆盖它。连续触发的教程按顺序播放，上一句的音频在 UE 播完后才开始下一句；最后一句结束后恢复收音与新问答，不继续播放已取消的旧答案。`AllowVoiceInterruptions` 仅用于普通对话，不能打断教程。

地点沿用当前星球关卡的路线与解锁条件，`路线进度 = -玩家 Y 坐标`：

| 地点 ID | 真实触发条件 | 默认原有台词来源 |
|---|---|---|
| `landing` | 本局玩家接入落地区 | 原有 Welcome 教程原文 |
| `training` | 首次通过路线进度 2150，进入训练检查点 | 原有 Shield 教程指令 |
| `ruins` | 进度超过 5300，四个训练目标及两组峡谷敌人已完成 | 原有关卡“Starcore ruins”目标原文 |
| `boss` | 进度超过 6200、两名遗迹守卫已击败，Boss 实际激活 | 原有关卡“Defeat the guardian”目标原文 |

每个区域每局只播报一次。停留、来回走动和检查点重试不会重复播放；重新开始整局会恢复触发。到达地点只触发台词，不替代任何技能或教程完成事件；玩家说“完成了”仍不能推进教程。

修改地点台词：选中关卡里的 `BP_PlanetDirector`，在 `Tutorial → Locations → RegionTutorialLines` 修改对应 ID 的文本。新增蓝图触发器可在真实区域进入回调中调用 Director 的 `AnnounceRegion(Region)`；已有教程/游戏事件会自动进入相同优先队列。其他蓝图预设台词调用角色 Integration 的 `PlayTutorialLine(Line)`，不要使用实时问答入口发送预设台词。连续相同的待播台词会合并，避免重复播报。

Grok 播报使用独立的原生语音连接，以 `force_message` 按原文合成、不经过聊天模型改写；普通对话连接保留上下文。只有 UE 确认播放完成后才释放教程优先级。网络失败时切换已有本地 TTS，原文字幕和优先级继续有效；本地 TTS 被关闭时保留字幕阅读时间。预设字幕来源为 `scripted`，普通真实 Grok 回答来源仍为 `grok`。

2026-10-18 已在 UE 中收到真实 Grok 预设教程音频（214560 字节 PCM），原文与蓝图回调一致；在线验证扩展为 17 项全部通过，日志 `Saved/Logs/TutorialPriority_LiveGrok.log`。未录制麦克风，真人听音仍由现场确认。教程修改前备份：`Saved/CodexBackups/TutorialPriority_20261018_025126`。更新后需重启 Python 服务并重新打开 UE，加载新的教程优先控制。

## 需要项目资源/配置

关卡真正敌人/Boss、生命/伤害数值及完整胜负闭环现已接入 `/Game/AlienPlanet/Lvl_AlienPlanet`，见 [外星行星关卡说明](ALIEN_PLANET_LEVEL.md)。
COSMO 伙伴专用动画资源仍需映射；Grok 账号需具备当前语音模型权限与额度。
对应蓝图入口、资源映射和配置位置已完成，详见功能文档。本地教学不会冒充真实模型。
