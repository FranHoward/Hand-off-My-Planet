HAND OFF MY PLANET - UI PNG + FONT EXPORT

Getting Started: extract the entire ZIP, then open Asset_Index.html to browse the assets.

Contents
- 134 PNGs at original size, representing 150 Figma asset nodes.
- 32 complete buttons, with backgrounds, outlines, glow, and labels exported together.
- 1 special-font main title PNG, preserving the original glyphs, colors, and effects.
- Screen images, tutorial gestures, arrows, ability icons, crosshairs, panels, progress bars, and other assets.
- 17 screens share one background stored in 00_Common. Each page's Screen_Assets.json points to it.
- Standard text: 3 static TTFs used in the design (IBM Plex Mono Regular, Medium, and Orbitron SemiBold), plus the official Orbitron variable font source. Complete licenses are included.
- Original text and style records for 238 text nodes, including 45 standalone game-screen texts and 96 asset-sheet texts. Button labels, hidden alternatives, and design annotations have separate purpose markers.

Directories
00_Common: shared background.
01_Game_Screens: 8 game screens, organized by screen and asset type.
02_Gesture_Tutorials: 9 tutorials, with images and arrows exported separately.
03_UI_Asset_Sheets: buttons, icons, and HUD assets from 6 asset-sheet groups.
04_Unplaced_Assets: HP bar assets not placed on a screen in the original file.
05_Typography: fonts, licenses, text CSV/JSON, and per-screen TXT files.
06_Manifest: asset paths, original nodes, PNG dimensions, screen positions, and complete properties.
07_Preview: button and image overviews. JPGs here are for browsing only; use the corresponding PNGs in-game.

Usage Notes
1. All PNGs were rendered directly from current Figma nodes at original 1x size, without upscaling, interpolation, or redrawing.
2. Button text is already composited. Do not overlay the same label after importing a button PNG.
3. Standard text remains editable. Use the original font, size, letter spacing, line height, color, and alignment from Text_Inventory.csv or JSON. AUTO line height is recorded as-is, without guessing pixel values.
4. PNGs retain original transparent edges, outer glow, and layout whitespace. Some button bounds differ from visible artwork, so PNG dimensions may exceed node dimensions. Assets.csv provides render positions; Assets.json retains node bounds and visible regions.
5. Existing black backgrounds in ability icons and original transparent backgrounds in tutorial gestures are retained. Black image content was not removed.
6. Design notes, guides, and hidden explanatory text were not exported as game PNGs. Use design_note / hidden_alternate in text records for comparison.
7. Font families and weights were checked against Figma. Font files came from official channels; see Font_Sources.json for exact sources.

Verification Results
- No visible graphic leaf nodes were omitted from the game-screen, tutorial, asset-sheet, or unplaced-assets pages.
- All 32 buttons contain their text nodes, and the button overview was checked.
- All PNGs were verified to open, have valid dimensions, and contain non-transparent content.
- The file inventory includes SHA-256 checksums; the final ZIP passed integrity checks.

Original Figma File: https://www.figma.com/design/JgBBcHeJzxFynEnYwnQaJG/Hand-off-my-planet
