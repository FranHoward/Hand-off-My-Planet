# 当前动作映射更新（2026-10-17）

本次只修改 Python 识别和 JSON。头部 yaw、左右手镜像修复、Fire 握拳后推出、Wave 横挥保持原来的实现。

| 动作 | 当前结果 |
|---|---|
| 左手张掌放在中立位置 | NONE，移动轴 0，不开盾 |
| 左手张掌前推/后拉 | FORWARD / BACKWARD |
| 左手握拳 | STOP，立即清零移动，不受距离影响 |
| 右手张掌保持不动 | SHIELD |
| 右手握拳后推出 | FIRE |
| 右手横向挥动 | WAVE |
| 右手拇指、食指指尖接触捏合 | LASER |
| 右手只伸食指 | 不再触发 Laser |

## 修改文件

- game_controls.py：左拳停止优先于距离判断；握拳不用于中立校准；停止后短暂未知不恢复移动。Shield 移回右手并检查静止。Laser 从 POINT 改为 PINCH，瞄准坐标改为 pinch_center。Wave 排除捏合及捏合状态过渡。
- gesture_recognition.py：更新命令帮助中的左右手分工。原调试界面自动显示新结果。
- test_game_controls.py / test_recognition_fixes.py：按新要求更新动作映射测试；保留头部、Fire、双手路由、JSON 回归。
- test_stop_shield_pinch.py：补充停止、距离无效化、重新张掌恢复、静止开盾、捏合瞄准、松开结束、指向不激活等 14 项回归测试。
- README.md / VALIDATION.txt / sample_packet.json：同步当前操作说明与输出样例。

JSON version 仍为 3，原事件和状态字段保持。left_hand=STOP、left_hand_actions=[STOP]、movement=Stop、move_axis=0 是左拳停止的表示。STOP 不加入 held_actions，避免当成持续移动输入；其中只保留仍有效的右手技能和头部动作。

当前只有摄像头和 JSON，没有接入游戏。代码输出停止移动指令，不会直接修改游戏人物速度。

## 保留的上一轮修复

模型在未镜像画面上推理，自拍镜像仅用于预览；手别按玩家左右定义，不按画面位置或结果顺序分配。
头部使用姿态矩阵估算角度，H 校准正中，具备平滑、迟滞、死区和丢脸立即 NONE。
Fire 仍为握拳后推出，保留短暂 NONE 容错、尺度确认和保持不连发。
既有 PINCH 几何分类和参考关键点回归保留，不新增模型或依赖。
