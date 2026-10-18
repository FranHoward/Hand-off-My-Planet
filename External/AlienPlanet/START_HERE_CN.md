# 外星火山行星场景包

统一风格：深色火山岩、切面造型、青蓝色导航与能量装置、橙色熔岩与危险核心，少量紫色用于 Wave。

本包有 **27 个独立模型 FBX + 3 个区域布局 FBX + 1 个完整地图 FBX**。完整布局共有 93 个摆放实例，约 7.24 万三角面，路线长度约 96 米。模型为本次制作的原创风格化原型。

## 最快使用方式

1. 先解压，保留 `textures/`。
2. 如果要直接获得已摆好的地图，在 Unreal 的 **File → Import Into Level** 中选择 `fbx/scenes/AlienPlanet_FullLayout.fbx`。场景导入可保留对象布局；只把 FBX 拖到 Content Browser 会主要作为模型资源导入。不同 UE 版本的导入界面可能略有不同。
3. 所有模型按 **Static Mesh** 使用，导入比例为 **1**。坐标采用厘米、Z 向上，模型正面为 -Y。使用 Unreal 的场景坐标转换选项；不要额外把比例乘 100。
4. 如果只要一个区域，选择对应的 `01_Spawn_Layout.fbx`、`02_Training_Layout.fbx` 或 `03_Arena_Layout.fbx`。三个文件保留完整地图中的世界位置，方便组合。
5. 如果要自己摆场景，使用 `fbx/assets/` 下的独立模型，重复摆放岩石、植物、岩壁和石柱。

官方参考：[FBX 场景导入](https://dev.epicgames.com/documentation/unreal-engine/fbx-scene-import-in-unreal-engine)、[FBX 导入选项](https://dev.epicgames.com/documentation/unreal-engine/fbx-import-options-reference-in-unreal-engine)。

## 三个区域

| 区域 | 已制作内容 | 布局用途 |
| --- | --- | --- |
| 01 飞船降落区 | 破损飞船、两种玄武岩、地面、青蓝发光植物、任务指引终端、熔岩裂缝 | 玩家出生、AI 介绍任务、移动教学 |
| 02 能量峡谷 | 青蓝/橙色水晶、模块化峡谷岩壁、能量屏障、Shield/Fire/Wave/Laser 四种攻击靶、小型悬浮机器人、步行桥 | 四种能力依次教学与小型遭遇 |
| 03 星核遗迹 | 20 米圆形战斗平台及入口坡道、岩石守卫 Boss、悬浮星核、发光符文石柱、充能装置 | Boss 战与星核充能 |
| VFX 网格 | 橙色火弹、紫色波动球、扩散环、青蓝碎晶、青绿光束端点 | 用作 Niagara Mesh Renderer 的几何组件 |

四种战斗能力仍为 **Shield、Fire、Wave、Laser**。青蓝碎晶是场景/受击视觉素材；补充参考词中的冰冻风格没有新增成战斗技能。充能/修复端点用于最终任务的视觉表现。

## 材质与发光

所有模型共享 `M_Planet_Atlas`，每个 FBX 都内嵌了四张 1024 × 1024 PNG，同时在 `textures/` 提供原图。

| 贴图 | Unreal 连接 | 颜色空间 |
| --- | --- | --- |
| Planet_BaseColor.png | Base Color | sRGB 开启 |
| Planet_Emissive.png | Emissive Color，可乘 2～4 调整 | sRGB 开启 |
| Planet_Roughness.png | Roughness | sRGB 关闭 |
| Planet_Metallic.png | Metallic | sRGB 关闭 |

FBX 材质传输不是完整着色器传输。导入后请检查这四个连接，尤其是 Emissive；复用同一个材质即可统一调整全场景。预览中的泛光需要 Unreal 的后处理设置，不能依赖 FBX 自动保留。

屏障另有 `M_EnergyField_Transparent` 材质槽。建议在 Unreal 新建 Translucent、Unlit、Two Sided 材质，青蓝色 `(0.015, 0.21, 0.28)` 接 Emissive，Opacity 从 `0.20` 开始调整。材质透明度设为 0 可以隐藏薄膜而保留边框与网格线。

UV0 用于共享色板图集，允许重叠。没有专门的非重叠 Lightmap UV；本包适合先用动态光照。如需烘焙静态光照，应另外展开 Lightmap UV。未生成 LOD 或进行目标设备帧率测试。

## 碰撞与游戏逻辑

- 地面和圆形平台先作为静态场景使用，按可行走表面设置碰撞。平台有向南入口坡道，避免把整个平台和坡道用单个凸包包住。
- 岩石、飞船、石柱可使用简单碰撞；峡谷岩壁按实际需求选择简化碰撞或静态复杂碰撞。
- 屏障的关闭/打开由单独的 Box Collision 和材质显隐控制，FBX 不含屏障逻辑。
- 植物、发光线条、碎晶和 VFX 网格通常无需碰撞。
- 建议先用 Unreal 角色在路线中走一遍，再调整胶囊体、坡度、碰撞和导航。这里没有 Unreal 运行环境，尚未做实机通行测试。

布局文件中的建议出生点是 Blender 坐标 `(0, -850, 120)` cm；导入 Unreal 的轴向可能经转换，请在地图南端飞船附近放置 PlayerStart。区域中心在 `scene_layout.json` 中。

**Boss 与小型机器人目前均为静态模型，没有骨骼、走路、攻击、死亡动画或 AI。** 可先整体移动/旋转 Actor 做原型，之后再绑定骨骼或拆分部件动画。教学进度、攻击命中、Boss 行为、星核充能都由你们现有 Blueprint/AI 系统连接。

**FBX 不包含 Niagara 系统、屏幕 HUD、UMG 控件或手势识别代码。** 包里的任务终端是场景内实体显示装置。真正的火花、拖尾、折射、光束与动态粒子需在 UE 中制作，见 `unreal/NIAGARA_RECIPES.md`。

## 源文件与目录

- `AlienPlanet_ThreeZones.blend`：完整场景源文件，贴图已打包。
- `00_MODULAR_ASSETS_REAL_SCALE`：源文件中的独立模型集合，默认隐藏，所有模型原点在世界零点。
- `01_Spawn` / `02_Training` / `03_Arena`：已经摆好的可见场景实例。
- `99_PREVIEW_LIGHTS_AND_BACKGROUND`：预览灯光和相机，未导出进 FBX。
- `preview/`：四张实际 Blender 渲染图，不是概念图。
- `asset_catalog.csv`：独立模型目录、尺寸、三角面数。
- `asset_manifest.json` / `scene_layout.json`：模型数据与摆放位置。
- `validation_report.json`：FBX 重新导入的实际检查结果。
- `source/`：可重复生成模型与场景的脚本。Blender 4.5 / bpy 4.5.14，依赖 numpy、Pillow。运行 `build_planet.py`；设置 `SKIP_RENDER=1` 可以只生成模型与源文件。

所有 31 个 FBX 已通过 Blender 回导校验：网格数量、三角面、非退化几何、UV、单位尺寸、模型原点、内嵌贴图及场景位置。尚未在 Unreal Engine 中实际导入运行。
