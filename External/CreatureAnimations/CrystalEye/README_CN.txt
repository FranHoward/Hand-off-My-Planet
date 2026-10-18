COSMO — 角色动画交付说明
========================================

内容
CrystalEye：3 个动作。Minion：6 个动作。Boss：7 个动作。
每个角色目录包含：SkeletalMesh FBX、独立动画 FBX、带材质/蒙皮/全部 Actions 的 Blender 源文件、Textures、Skeleton.json、events.json、独立 MP4 预览。
Boss 保留原有六足岩甲兽结构；规范中的手臂/双腿动作按六足结构改编为前肢攻击与六足承重，没有添加人体器官或装备。

时间与帧数
统一 30 FPS。一个 N 帧时长的动作导出范围为 0..N，含 N+1 个采样点，实际时长严格为 N/30 秒。末端采样用于准确结束和无缝循环。
Blender 循环播放使用 0..N-1；第 N 帧是闭环端点。
Action 的第 -1 帧是中立绑定参考姿势，不属于动作、不导出。请勿以整个 Action 范围重新导出；按 events.json 的 0..N 范围导出。
单次动作视频前后增加短暂停留，便于看清动作；这些停留不包含在动画 FBX 中。循环视频展示两个周期。

动作清单（全部 30 FPS）
CrystalEye_Idle          3.0s / 90 帧 / 循环
CrystalEye_FloatForward  1.0s / 30 帧 / 循环
CrystalEye_Controlled   1.0s / 30 帧 / 单次
Minion_Idle             2.0s / 60 帧 / 循环
Minion_Walk             0.8s / 24 帧 / 循环
Minion_Attack           0.9s / 27 帧 / 单次
Minion_Hit              0.5s / 15 帧 / 单次
Minion_Death            1.2s / 36 帧 / 单次，保持最终姿势
Minion_Jump             0.9s / 27 帧 / 单次
Boss_Idle               3.0s / 90 帧 / 循环
Boss_Walk               1.2s / 36 帧 / 循环
Boss_Attack             1.2s / 36 帧 / 单次
Boss_Hit                0.6s / 18 帧 / 单次
Boss_Death              2.0s / 60 帧 / 单次，保持最终姿势
Boss_Pounce             1.5s / 45 帧 / 单次
Boss_Jump               1.3s / 39 帧 / 单次

打开 Blender
1. 打开角色目录中的 .blend。贴图已内嵌。
2. 选中 Armature，在 Dope Sheet -> Action Editor 切换命名 Action。
3. 设定时间范围 0..N（循环播放 0..N-1），按空格播放。
4. 默认隐藏视图叠加以便看模型；打开 Overlays 可查看骨骼和控制器。
5. 烘焙动作使用 FK。Armature 自定义属性 IK_LF、IK_RF 等默认为 0；为制作新姿势，可先取消当前 Action，将对应属性调至 1，然后移动 CTRL_Foot_*。腿部 IK 禁止伸缩。控制器不在 FBX 中。
6. Root 为模型内部固定基准；body 是可活动节点。两者有意不强制首尾连接，防止 Blender FBX 自动连接时抹除 body 的位移。整体仍是唯一 Root 的完整父子层级。

导入 Unreal Engine 5
1. 每个角色先导入 *_SkeletalMesh.fbx：Skeletal Mesh 开启，Import Animations 关闭，Skeleton 留空创建该角色的 Skeleton。导入缩放 1，Convert Scene / Convert Scene Unit 开启。
2. 再导入该角色独立动画 FBX：Import Mesh 关闭，Import Animations 开启，选择第一步创建的同一 Skeleton，Animation Length 用 Exported Time，采样率 30。
3. 不启用 Use T0 As Ref Pose；不要用动画首帧覆盖 Skeleton 的绑定姿势。
4. 全部是 In-Place：禁用 Root Motion。不要混用引擎位移和根运动。
5. 模型采用厘米，Blender -Y 为前方、+Z 为上方；同一角色所有 FBX 使用同一坐标转换。若项目 MeshComponent 朝向需要调整，请统一调整组件相对旋转，不要对某一动画单独改导入轴向。
6. Walk 匹配速度：Minion 约 80.882 cm/s，Boss 约 77.381 cm/s。需要更快时同步调整动画播放倍率与移动速度；否则会出现游戏中的滑步。
7. Jump 的世界高度/抛物线和 Pounce 的向前距离由 UE 控制。预览视频中额外的整体跳跃/扑击轨迹仅用于展示，已明确标注，未写入 FBX。
8. FloatForward 也是原地姿态循环。使用状态机混合恢复到 Idle，建议从约 0.1–0.2s 的混合时间开始调整。
9. Death 不循环，播放完成保持最后一帧；不要回到 Idle。
10. Skeleton 包含在 SkeletalMesh FBX 内；Skeleton.json 是可读骨骼结构说明，不是 Unreal .uasset。UE 的 Skeleton .uasset 在导入时生成。

材质
保留原始 BaseColor / Emissive / Roughness 图集；Textures 中提供原图。
如果 UE 自动材质未接全：BaseColor -> Base Color；Emissive -> Emissive Color（乘以 2 可接近源文件强度）；Roughness -> Roughness，关闭 Roughness 的 sRGB。源材质 Metallic 约 0.18。亮度仍会受到 UE 的灯光、曝光和后处理影响。

攻击事件与 1 秒控制同步
FBX 不会将 Blender 标记变成 Unreal AnimNotify。交付包括每个 Action 的 pose markers 及 events.json，必须在 UE 动画编辑器按同名事件创建 Notify，并接入游戏逻辑。
Minion_Attack：第 11 帧，0.366667s，Attack。
Boss_Attack：第 17 帧，0.566667s，Attack。
Boss_Pounce：第 17 帧 0.566667s Launch；第 29 帧 0.966667s Hit_Controlled_1s；第 33 帧 1.1s Land。
Boss_Pounce 第 29 帧执行实际碰撞/命中检查。只有命中晶体眼睛才设置 Controlled，并从第 0 帧播放 CrystalEye_Controlled。
CrystalEye_Controlled 在其本地 1.0s（第 30 帧）结束控制。推荐由游戏状态计时器管理精确的 1 秒持续时间，动画提供视觉反馈。不要在 Boss 动画结束时提前解除控制。
同步例子：Pounce 开始 t=0；命中 t=0.966667；晶体眼睛解除控制 t=1.966667。Boss 自身在 t=1.5 完成动作。
攻击伤害、控制、碰撞、VFX、镜头震动以及 Energy Wave 打断逻辑不包含在 FBX 中。没有增加新的武器或技能。

验证范围
详见 VALIDATION_REPORT.txt、FBX_Validation.json、Animation_Quality.json。
验证包含实际 Blender 源文件重开、所有 FBX 回读、骨骼层级、绑定姿势、逐帧蒙皮位置比较、动画时长、固定 Root、循环端点、刚性部件不拉伸、地面高度及脚部接触。
当前环境没有 Unreal Editor，未执行 UE 编辑器内导入或游戏运行测试；不要把 Blender 回读验证等同于已在你们的 UE 项目里联调通过。
FBX 为 Blender 的真实二进制 FBX 7.4 (7400)。Epic 文档说明 UE FBX 管线基于 2020.2；本交付通过 Blender 的独立导入回读检查，仍应在项目中完成最终验收。

官方导入参考
https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-animation-pipeline-in-unreal-engine
https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-import-options-reference-in-unreal-engine
https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AnimationLibrary?application_version=5.6
