# Niagara 接入建议

以下是本项目的视觉设计起点，不是已编译的 Niagara System。FBX 仅提供静态网格；数值需要结合你们的移动速度、相机、曝光和性能预算调整。

Niagara 的 Mesh Renderer 可渲染静态模型，Ribbon Renderer 用于条带轨迹，Sprite Renderer 用于火花等面片粒子。材质需启用对应的 Niagara 使用标记。

官方参考：[Niagara Renderers](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-module-reference-for-niagara-effects-in-unreal-engine)。

## 四种战斗能力

| 能力 | 复用模型/对象 | 建议效果构成 | 起始数值（设计建议） |
| --- | --- | --- | --- |
| Shield | 你们已有盾牌 + FXM_CrystalShard | 受击短闪、少量青蓝碎晶、边缘波纹 | 碎晶 8～16 个，寿命 0.15～0.4 秒 |
| Fire | FXM_FireBolt | 橙色发光主体 + Ribbon 拖尾 + 少量 ember Sprite | 拖尾寿命 0.12～0.25 秒；火花寿命 0.15～0.45 秒 |
| Wave | FXM_WaveOrb + FXM_WaveRing | 紫色球体、扩散环、短暂波动；折射另做材质 | 扩散环 0.4～0.8 秒；半径约 0.5→3 米 |
| Laser | FXM_TealBeamEndpoint | 青绿色光束、明亮细芯、终点粒子收敛 | 端点直径约 0.1～0.4 米；命中闪光约 0.1 秒 |

`FXM_FireBolt` 的建模正面为 -Y。若 Niagara 的速度朝向按其他轴设置，请在 Mesh Renderer 或 Mesh Orientation 中补上旋转，使弹头朝前。

`FXM_WaveRing` 的原始半径为 1 米，可直接通过缩放形成扩散。不要把每个环的网格三角面数量乘以几千个粒子；原型只需少数几个环。

`FXM_CrystalShard` 是青蓝环境碎片/盾牌受击碎片，不新增 Ice/Freeze 技能。冻结参考图中的晶体色彩可用作视觉参考。

## 星核充能 / 修复

- 起点：玩家或充能装置。终点：StarCore 物体中的发光核心位置。
- 模型的核心中心约位于自身原点上方 175 cm；装置顶部端点约上方 210 cm。
- 使用青绿色细光束连接起终点；终点叠加 `FXM_TealBeamEndpoint`。
- 粒子向终点收敛，随着充能值增加提高密度或亮度。
- 进度来自真实游戏状态，不能靠播放粒子时间直接当作任务完成。

## 场景环境效果

- 熔岩裂缝：沿 `LavaFissure_8m` 放低密度橙色余烬，向上慢飘。
- 水晶：只在附近放少量缓慢上浮的微光；图集的发光已经提供主体辨识。
- Boss：橙色心脏是视觉弱点，受击时局部闪亮；裂缝上可加少量火花。
- 能量屏障：通过材质透明度/UV 扫描实现变化，碰撞开关独立控制。

## HUD 风格对接

屏幕 HUD 和手势跟踪显示在 UMG 中制作，不属于 FBX 模型。

- 主色青蓝，提醒色琥珀；用简短标记展示当前技能和识别状态。
- 四种能力分别为 Shield / Fire / Wave / Laser，与训练靶图案保持对应。
- 手势置信度不足时显示“识别中”，不要直接触发技能。
- 任务终端使用场景内显示；具体教程文字由 AI/任务状态驱动。
- 不在素材中预设 Fire、Wave 或 Laser 的具体手势，以你们识别队友的定义为准。
