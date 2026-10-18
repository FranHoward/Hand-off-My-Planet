HAND OFF MY PLANET — UI PNG + FONT EXPORT

打开方式：先解压整个 ZIP，再打开 Asset_Index.html 浏览资源。

内容
- 134 张原尺寸 PNG，对应 Figma 的 150 个资源节点。
- 32 个完整按钮：底图、描边、光效与按钮文字一起导出。
- 1 张特殊字体主标题 PNG：保留原字形、颜色与效果。
- 界面图片、教程手势、箭头、技能图标、准星、面板、进度条等均已导出。
- 17 个界面共用同一背景，统一放在 00_Common；各页面 Screen_Assets.json 指向该文件。
- 普通文字：3 个实际使用的静态 TTF（IBM Plex Mono Regular、Medium、Orbitron SemiBold），以及 Orbitron 官方可变字体源文件；完整授权随包提供。
- 238 个文本节点的原文与样式记录。其中游戏界面独立文字 45 个，素材板文字 96 个。按钮文字、隐藏副本、设计标注均另有 purpose 标记。

目录
00_Common：共用背景。
01_Game_Screens：8 个游戏界面，按界面和资源类型分目录。
02_Gesture_Tutorials：9 张教程，图片与箭头分开导出。
03_UI_Asset_Sheets：6 组素材板中的按钮、图标和 HUD 素材。
04_Unplaced_Assets：原文件未放入画面的 HP 条素材。
05_Typography：字体、授权、文字 CSV/JSON、按页面拆分的 TXT。
06_Manifest：资源路径、原节点、PNG 尺寸、画面位置与完整属性。
07_Preview：按钮及图片总览。此目录的 JPG 只供查看，游戏使用对应 PNG。

使用要点
1. 所有 PNG 直接从当前 Figma 节点以 1× 原尺寸渲染。没有放大插值或重新绘制。
2. 按钮文字已经合成，导入按钮 PNG 后不要再次叠加同一段标签。
3. 普通文字保持可编辑，按 Text_Inventory.csv 或 JSON 使用原字体、字号、字距、行高、颜色和对齐。AUTO 行高原样记录，没有猜测像素数值。
4. PNG 保留原有透明边缘、外发光及布局空白。部分按钮的外框尺寸与实际图形不同，所以 PNG 尺寸可能大于节点宽高；Assets.csv 给出渲染位置，Assets.json 保留节点边界和可见区域。
5. 技能图标原图中已有的黑色底色保留，教程手势原有的透明背景保留。没有把黑色图像内容擅自抠除。
6. 设计说明、辅助线和隐藏说明文字未当作游戏 PNG 导出；文本记录中的 design_note / hidden_alternate 可用于核对。
7. 字体家族与字重按 Figma 核对；字体文件从官方渠道取得，具体来源见 Font_Sources.json。

核对结果
- 游戏界面、教程、素材板、未放置素材四个页面：没有遗漏的可见图形叶节点。
- 32 个按钮均包含文字节点，已检查按钮总览。
- 所有 PNG 已验证能打开、尺寸有效、内容非全透明。
- 文件清单含 SHA-256 校验值；最终 ZIP 已进行完整性检查。

Figma 原文件：https://www.figma.com/design/JgBBcHeJzxFynEnYwnQaJG/Hand-off-my-planet
