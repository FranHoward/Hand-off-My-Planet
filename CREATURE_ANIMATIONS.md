# 三种生物的动画蓝图

UE 5.7。三套独立骨骼、16 段动画、材质和贴图已导入 `Content/Creatures`。ABP 内包含可编辑的 `CreatureStateMachine`、Sequence Player 和转换规则；共同父类为 `CosmoCreatureAnimInstance`，负责读取移动和维护动作优先级。

| 生物 | 动画蓝图 | 状态 |
| --- | --- | --- |
| 水晶眼 | `/Game/Creatures/CrystalEye/ABP_CrystalEye` | Idle、Move（FloatForward）、Controlled |
| 小怪 | `/Game/Creatures/Minion/ABP_Minion` | Idle、Move（Walk）、Attack、Hit、Jump、Death |
| Boss | `/Game/Creatures/Boss/ABP_Boss` | Idle、Move（Walk）、Attack、Hit、Jump、Pounce、Death |

每个目录有一个 `BP_<生物>_AnimationPreview`，拖入关卡即可观察待机。预览 Actor 已绑定对应网格、材质和 ABP，没有碰撞和敌人逻辑。预览展示的是源资产外形，实际角色尺寸、碰撞和朝向由玩法角色设置。

## 接到角色

1. Skeletal Mesh 组件选用同一目录的 `SK_<生物>`，Animation Mode 选择 Use Animation Blueprint，Anim Class 选择对应 ABP。
2. `Get Anim Instance` → Cast to `CosmoCreatureAnimInstance`。角色移动时默认读取拥有者的平面速度；Character 还会读取 `IsFalling` 并自动进入 Jump。
3. 攻击、受击、主动跳跃、Boss 扑击分别调用 `RequestAttack`、`RequestHit`、`RequestJump`、`RequestPounce`。返回 true 表示请求已接受。不存在的动作返回 false；水晶眼没有攻击、受击和死亡动画，不会借用其他生物骨骼。
4. 小怪或 Boss 死亡时调用 `SetDead`。死亡不可被普通动作打断，非循环播放后保持末帧。复活时先恢复角色生命值和死亡标记，再调用 `ResetForRespawn`。拥有者若有数值 `Health` 或布尔 `bDead`，也会自动读取死亡条件。
5. 绑定实例的 `OnAnimationEvent(EventName)`，接入下表的碰撞窗口、脚步声和特效。伤害、位移、真实命中检测及敌人决策应由玩法 Actor / AI 执行。动画通知只发出事件，不直接扣血。

移动采用 5 cm/s 进入、2 cm/s 退出的滞回阈值，避免静止抖动。Move 播放倍率为速度 / `ReferenceMoveSpeed`，限制 0.1–4；小怪参考速度 80.882 cm/s，Boss 77.381 cm/s。水晶眼的默认参考速度暂为 80.882 cm/s，可在 ABP Class Defaults 按实际飞行速度调整。测试或非 Character 的自定义移动可使用 `SetManualMovement(Speed, IsInAir)`，通过 `ResumeOwnerMovement` 恢复读取拥有者。

动作优先级为 Death > Controlled > Hit > Pounce > Jump > Attack > 移动/待机。高优先级动作可打断低优先级动作；Attack/Pounce 不接受逐帧重复触发，重复 Hit 会重新开始受击。单次动作结束后恢复移动/待机，Jump 在仍悬空时保持末帧，落地后恢复。

## 通知和水晶眼控制

所有源 `events.json` 的通知均在动画 `COSMO Events` 轨道中，按 30 fps 原始时间建立；根运动关闭，跳跃和扑击的世界位移由玩法处理。

| 动画 | 通知时间（秒） |
| --- | --- |
| CrystalEye Controlled | ControlBegin 0；ControlEnd 1.0 |
| Minion Walk | Footstep_LF 0；Footstep_LB 0.2；Footstep_RF 0.4；Footstep_RB 0.6 |
| Minion Attack | Attack 11/30（0.366667） |
| Minion Jump | TakeOff 0.2；Land 20/30（0.666667） |
| Boss Walk | Footstep_LF / RM / LB 0；Footstep_RF / LM / RB 0.6 |
| Boss Attack | Attack 17/30（0.566667） |
| Boss Pounce | Launch 17/30；Hit_Controlled_1s 29/30；Land 1.1 |
| Boss Jump | TakeOff 0.3；Land 0.9 |

Boss 收到 `Hit_Controlled_1s` 时，玩法进行碰撞或范围检测，**确认命中后**对目标水晶眼的动画实例调用 `ApplyControlled(1.0)`。播放扑击本身不会自动判定命中。水晶眼的控制计时独立于 Boss：例如扑击 0.966667 秒命中，控制持续到 1.966667 秒，不会在 Boss 1.5 秒动画结束时提前解控。再次确认控制命中会从该次调用重新计算完整时长。

`ControlBegin` 在控制请求被接受时发出，`ControlEnd` 由独立计时器发出；动画首尾的同名通知用于标记，不会重复触发生命周期。被受击或死亡打断的旧攻击通知会被过滤。每次单次动作的同名玩法事件最多发出一次，脚步事件按移动循环发出。

在玩法角色中将 `ControlBegin` / `ControlEnd` 连接到移动、输入的禁用与恢复。ABP 管理姿态和计时事件，Actor 的实际移动限制仍由玩法角色执行。

## 重新生成与验证

已通过三套 ABP/预览蓝图的编译（零错误、零警告）和 128 项 UE 实际骨骼播放检查，包括骨骼与时长、材质槽绑定、通知帧、移动倍率、受击打断、攻击事件去重、跳跃落地、扑击事件、一秒控制与刷新、死亡末帧和复活。

```powershell
# 默认仅构建插件并验证已有资产，保留手工编辑的 ABP
.\Scripts\VerifyCreatureAnimations.ps1

# 显式重新生成状态机、预览 Actor 与 COSMO 通知轨道，然后验证
.\Scripts\VerifyCreatureAnimations.ps1 -RebuildAssets

# 从已解压的源 FBX/贴图补充导入，再重新生成并验证
.\Scripts\VerifyCreatureAnimations.ps1 -ImportSources
```

`-SkipBuild` 可复用当前插件构建；引擎路径可用 `-EngineRoot` 指定。源包位于 `External/CreatureAnimations`，压缩包哈希见 `source_manifest.json`。导入脚本保留现有已导入资产；源内容改动需在编辑器中显式重导入后再生成。

检查结果：`Saved/CreatureAnimationVerification.json`，完整过程：`Saved/Logs/CreatureAnimation_Verify.log`。离屏预览截图：`Saved/Screenshots/Creatures_Idle.png`、`Creatures_Move.png`、`Creatures_Actions.png`。

可使用 `UnrealEditor.exe VFX.uproject "/Engine/Maps/Entry?game=/Script/Engine.GameModeBase" -game -CreatureAnimationPreview -RenderOffscreen -windowed -ResX=1600 -ResY=900 -ForceRes -NoSound -unattended -dx11` 重现截图。此预览仅在编辑器构建中启用，创建临时场景并自动退出，不更改游戏地图。
