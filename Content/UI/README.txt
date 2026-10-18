UI 入口与使用

所有运行中的界面由 /Game/UI/HUD/BP_PlanetHUD（AHUD）绘制。
原生父类为 CosmoGameHUD；没有为每张教程创建单独的 HUD 或 Widget。
24 组布局数据保存在 DA_PlanetUI；其中包括游戏界面、9 张教程及素材表。
External/UI/Layout.json 是可重新导入的布局源数据。External/UI/Source 保留用户提供的原始素材。
Textures、Fonts 是正式导入的 Unreal 资产。

Project Settings / Maps & Modes：
Game Default Map 与 Editor Startup Map 均为 /Game/UI/Maps/Lvl_MainMenu。
开始界面专用 GameMode 不生成游戏角色。
START MISSION → SKIP TUTORIAL? 弹窗（居中，占视口约 59.3% 面积）。
CONFIRM 跳过教程，直接进入 /Game/AlienPlanet/Lvl_AlienPlanet；CANCEL 连接 Grok，进入开场对话和 9 张教程后进入同一关卡。
弹窗由同一 BP_PlanetHUD 绘制，沿用深色面板、青色边框和现有字体；默认焦点为 CANCEL，Esc 同样继续教程。
教程共用一套 HUD，淡入/淡出各 0.55 秒；音频响应完成且本机音频缓冲播放完毕后才换页。
真实 Grok 服务必须在 ws://127.0.0.1:8766 运行，使用项目现有 Scripts/StartCosmo.ps1。
服务不可用时等待连接；朗读中断时停留当前页，Enter/手柄确认重试。
不会用固定时长或本地 TTS 跳过教程。Esc 可取消教学返回开始界面。

鼠标点击；方向键/Tab/手柄方向键选择；Enter/Space/手柄确认键执行。
游戏中 Esc/手柄返回键暂停，再按恢复。
暂停菜单包括恢复、重启确认、语音设置、返回主菜单。
胜负界面接入角色事件，结果页显示实际击杀、进度、星核和剩余生命。
战斗 HUD 接入生命、护盾、技能冷却、Boss 生命、目标、字幕和摄像头预览。

验证：Scripts/VerifyUI.ps1 -Build -RegenerateAssets -Preview
测试仅使用本机模拟音频，不调用 Grok，不访问摄像头或麦克风。
检查语音失败后原页重试、10 页实际缓冲播放完成、关卡加载、暂停恢复、无音频时不跳页。
离屏截图位于 Saved/Screenshots/UI_*.png。
真实 Grok 凭据、服务、扬声器及摄像头仍需在实际游玩环境中运行验证。

教程结束后的对话：
进入关卡后默认启用 FreeConversation，关闭地点播报、技能教学和自动“下一步”提问。
HUD 顶部不再显示教程行动指引；开场和 9 张前置教程不受影响。
玩家可直接自由聊天，也可以主动向 Grok 询问游戏问题。
安静约 75–150 秒后可触发一次 Grok 主动闲聊；玩家说话或 Grok 回复后重新计时。
战斗、暂停、失去窗口焦点、静音、死亡/胜利和已有对话期间不主动开口。
主动话题由 Grok 生成，不用伪造的玩家消息或固定教学台词。

互动教程（按提供的九项表格）：
左手握拳 → 左手前伸 → 左手后收 → 右手张开静止 → 捏合 → 松开 → 握拳前推 → 水平挥手 → 头部左右转动。
每页英文操作指令和成功反馈保存在 External/UI/Layout.json 的 tutorials 数组，并导入 DA_PlanetUI。
指令播完后等待动作；动作成功后显示并由 Grok 朗读反馈；反馈播完才淡出换页。
稳定动作需要约 0.45 秒；转头需要左右两个方向；松开捏合要求先确认过捏合，且松开时手仍被跟踪。
Fire/Wave 仅接受本页指令播完后新的识别事件；空帧、丢失跟踪或重复旧事件不会完成教程。
教程开始时启动摄像头识别并显示预览；取消或进入关卡时释放教程摄像头进程和接收端口。
在 BP_PlanetHUD 的 Tutorial|Camera 可设置摄像头编号、左右手交换和转头反向。
验证包含模拟 9 个手势、19 段完整音频（开场 + 9 指令 + 9 反馈）、指令和反馈失败重试。

布局 JSON 和原始素材已移到项目 External/UI，避免 Unreal 自动将 JSON/CSV 作为 DataTable 导入。正式资产仍在 Content/UI。请勿将布局 JSON 拖入内容浏览器；游戏使用已经生成的 DA_PlanetUI。

开始按钮与连接：
主菜单打开时不连接语音；点击 START MISSION 才启动游戏内置的 COSMO 服务。
连接、上游会话和首段语音缓冲期间保持黑屏。首段音频就绪后，同一帧显示开场 UI 并播放语音。
内置服务由 GameInstance 管理，切换地图时继续运行，退出游戏时结束。
连接超时、密钥缺失等错误显示原因；Enter 重试，Esc 返回。
无需打开浏览器或手工启动 Python。Windows 用户环境变量 XAI_API_KEY 填入原有 Grok 密钥即可。
也支持 GROK_API_KEY 别名。只用 Grok Voice 不要求配置 XAI_MODEL。
启动器在每次启动时读取持久化的用户/系统环境变量，兼容早于变量设置启动的编辑器。
仅在 CMD 使用 set 设置的临时变量不能被无关的游戏进程读取；请使用 Windows 环境变量设置保存。
新增验证选项：Scripts/VerifyUI.ps1 -Bootstrap；用明确的 mock 配置验证真实内置服务启动，不调用外部 Grok。
