# 热盒呼出失败：原因清单与排查手册

> 适用版本：Cavalry 2.8.1（Windows）+ Cavalry_ShortCut
> 记录时间：2026-11-18
> 说明：这份文档总结了开发过程中**实际遇到过**的每一条"呼出失败"，按"症状 → 原因 → 修法"排列。

---

## 一、先说结论：当前唯一可用的呼出链路

```
Cavalry 快捷键（Shortcut Manager 绑定）
   → 脚本 %APPDATA%\Cavalry\Scripts\Cavalry ShortCut 呼出.js
   → 写 %TEMP%\CavalryShortCut.show（内容 show + 时间戳）
   → Cavalry_ShortCut.exe 每 5ms 检查该文件的"修改时间"
   → ShowAtCursor()：出现在鼠标位置 + HWND_TOP
```

任何一环断了都会表现为"按了没反应"。下面按环节列原因。

---

## 二、脚本环节（最常见）

### 1. 脚本文件名改过，导致快捷键绑定失效 ⚠️ 踩过

Cavalry 的快捷键绑定记录在 `%LOCALAPPDATA%\Cavalry\shortcuts.json`，**按绝对路径 + 文件名**作为 key：

```json
"C:\\Users\\...\\Scripts\\Cavalry ShortCut 呼出.js": "Shift+A"
```

只要把脚本**改名/移动**，这条绑定就指向一个不存在的文件 → 菜单里还能点，快捷键完全无效。

**排查**：打开 `shortcuts.json`，确认 key 的文件名与实际文件**逐字符一致**。
**修法**：改回原名，或重新在 Shortcut Manager 里绑定。

### 2. 脚本没放进正确目录 / 没重启 Cavalry

- 正确目录：`%APPDATA%\Cavalry\Scripts\`（注意是 **Roaming**，不是 Local）
- 新增脚本后必须**重启 Cavalry** 才会被扫描进 Shortcut Manager

### 3. `api.runDetachedProcess` 在 2.8.1 不存在

文档里有，运行时报 `TypeError: api.runDetachedProcess is not a function`。
2.8.1 运行时只有 `api.exec`（执行 Cavalry 脚本），**没有**启动外部程序的 API。
→ 所以不能用脚本直接拉起 exe，只能"写信号文件让已在运行的 exe 响应"。

### 4. 脚本里的 API 用错

可用且已验证：

```js
api.getTempFolder()                       // 返回临时目录
api.writeToFile(path, content, overwrite) // 第三参可选
```

两者都在 `@JS_GUI_API` 命名空间，Cavalry 2.8.1 实测可用。

---

## 三、信号文件环节

### 5. 依赖"删除文件"来防重复 → 删除失败就无限触发 ⚠️ 踩过

早期实现是"发现文件 → 删除 → 显示"。若 `DeleteFileW` 失败（被占用/权限），文件一直在 → **每 5ms 触发一次**，表现为热盒疯狂闪烁、抢焦点、无法在其它应用打字。

**修法**：改用**文件修改时间**判定（`GetFileAttributesExW` + `CompareFileTime`），同一次写入只响应一次，不依赖删除。

### 6. 路径不一致

exe 用 `_wdupenv_s(L"TEMP")` 拼路径，脚本用 `api.getTempFolder()`。
两者通常一致，但 8.3 短路径（`HUANGL~1`）与长路径混用时要注意**归一化**。
**排查**：日志里打 `trigger path=`，和脚本实际写的路径比对。

---

## 四、显示环节（最隐蔽）

### 7. `IsWindowVisible == true` 但窗口被遮挡 ⚠️ 踩过两次

触发器里曾有：

```cpp
if (!IsWindowVisible(hwnd) || g_mode != M_PIE) ShowAtCursor(hwnd);
```

窗口被 Chrome / Cavalry 盖住后，`IsWindowVisible` **仍然是 true**，于是不再调用 `ShowAtCursor` → 永远不重新置顶 → 用户以为"呼出失败"。

**修法**：触发时**无条件**调用 `ShowAtCursor(hwnd)`，每次都重新定位 + 置顶。

### 8. 只调用 `SetWindowPos(HWND_TOPMOST)` 不生效 ⚠️ 踩过

后续的 `SetWindowPos(hwnd, HWND_TOP, ...)`（`ForceForeground` 里）会把 topmost 状态冲掉。
**若要真正置顶**：创建窗口时就要带 `WS_EX_TOPMOST`。

**注意**：当前版本**故意不用常驻 TOPMOST**——要求"其它应用窗口能盖住热盒"。

### 8b. 切走再切回来后，`HWND_TOP` 已经不够用了 ⚠️ 踩过（最坑）

现象：第一次呼出正常；切到别的窗口再切回 Cavalry，再呼出——日志一切正常（`after show: vis=1`），但屏幕上什么都看不见。

原因：热盒是 `WS_EX_NOACTIVATE` 的非激活窗口。当 Cavalry 被**激活**后，单纯的 `SetWindowPos(hwnd, HWND_TOP, ...)` **无法越过当前激活窗口**，只能排在它下面。

**修法：TOPMOST 闪一下再取消**

```cpp
SetWindowPos(hwnd, HWND_TOPMOST,   x, y, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);  // 必然盖住前台
SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, w, h, SWP_NOACTIVATE);                    // 立刻取消
```

效果正好是想要的：**呼出时一定在最前**，但**之后点别的窗口仍能盖住热盒**。

**验证方法**：用 `WindowFromPoint(窗口中心)` 看最上层是谁——
返回自己的类名才算真的在最前面（`IsWindowVisible` / `PrintWindow` 都判断不出来）。

### 8c. 重新呼出时先闪一下上一次的界面（搜索面板）

现象：在搜索面板里按 `Esc` 关闭后，再呼出会看到搜索面板一闪，然后才变成圆盘。

原因：`ShowWindow` 先显示，模式状态和重绘在之后才生效 → 第一帧画的是旧内容。

**修法**（三步都要）：

```cpp
if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);  // 1. 先藏
g_mode = M_PIE; SyncEditFromMode();                     // 2. 复位到圆盘
InvalidateRect(hwnd, nullptr, TRUE); UpdateWindow(hwnd); // 3. 先画好
SetWindowPos(hwnd, HWND_TOPMOST, ...);                  // 4. 再显示
```

另外，`Esc` 关闭时也要把 `g_mode` 复位成 `M_PIE`，否则下次显示的第一帧仍是搜索态。

### 9. `SWP_NOACTIVATE` 导致键盘事件收不到

热盒以 `SWP_NOACTIVATE` 显示时**不获取键盘焦点**，`WM_KEYDOWN` 收不到空格/反引号，空格会落到 Cavalry 触发"播放"。
**修法**：热盒显示期间临时 `RegisterHotKey(VK_SPACE)`，隐藏时注销；被遮挡时也注销（`WindowFromPoint` 判断）。
另外：如果 Cavalry 里有文本输入框正被编辑，系统会把焦点"粘"在它上面，热盒拿不到键盘——需要在显示后 **300ms 内持续抢焦点**（`AttachThreadInput` + `SetForegroundWindow`）。

### 10. 瞬时 `GetForegroundWindow() == null` 误杀

曾用"前台不是 Cavalry 就收起"的规则，切换窗口瞬间前台为 `null`，窗口刚显示就被自己收起。
**修法**：忽略 `null`；且已**整体取消自动关闭**（改为手动关闭），此坑不再存在。

### 10b. `return 0` 放错花括号层级 → **所有左键点击失效** ⚠️ 踩过（最冤）

现象：点扇区没反应、点列表没反应、只有个别按钮还能用。

原因：图标区是一个块：

```cpp
{                                   // 图标块
    if (齿轮命中)   { ...; return 0; }
    if (文件夹命中) { ... 打开 actions ... }
    return 0;                       // ← 放这里 = 任何点击都提前返回
}
```

`return 0` 一旦落在块尾（而不是 `if (文件夹命中)` 内部），**所有后续的页签/扇区/列表点击都被拦截**，表现为"左键全面失效"。

**修法**：`return` 必须写在对应的 `if` 分支**内部**：

```cpp
if (文件夹命中) {
    ...;
    return 0;   // 只拦截这一种点击
}
```

**教训**：给某个分支补 `return` 时，先确认它落在哪个 `{ }` 里——
用日志（`click at x,y` + 各分支自己的 log）验证，比盯着代码猜快得多。

### 10c. 「打开 actions 文件夹」始终打不开

`ShellExecuteW(explorer.exe, path)` 会弹 `0xc0000142`（进程启动失败）。

依次尝试这三种方式，并记录日志：

```cpp
SHOpenFolderAndSelectItems(pidl)       // ① Shell 原生：打开并选中
Shell.Application → ShellExecute(dir)  // ② 走 Explorer 的 COM 服务
ShellExecuteW(nullptr, L"open", dir)   // ③ 系统关联
```

若都失败 → **把路径复制到剪贴板**（`SetClipboardData(CF_UNICODETEXT)`）并写日志。

**注意**：由受限令牌启动的 exe（例如被脚本/CLI 拉起）常常三种都失败：
日志会看到 `PIDL fail / COM fail / folder -> 5`，但屏幕上没有窗口。
判断方法：**自己双击 exe** 再点一次；若此时能打开，就是令牌/会话限制，不是代码问题。

---

## 五、执行环节（点击扇区没反应）

### 11. 桥要求先 `read_preamble`

直接 `POST /call {"name":"execute_script",...}` 会返回：

```
ERROR: Read the preamble (read_preamble) before calling execute_script.
```

而热盒日志只判断 `"isError":false` 就打印 `ok`，属于**假成功**——看起来执行了，实际什么都没发生。
**修法**：执行前先发一次 `read_preamble`；若返回里出现该错误则重发并重试一次。

### 12. 桥/端口没起来

- Cavalry 侧：`Preferences → Enable MCP Server`（默认 6768）
- 桥：`cavalry_bridge.mjs` 监听 `127.0.0.1:6769`
- 日志第一行 `bridge ok` / `bridge not connected` 可直接判断
**排查**：`Get-NetTCPConnection -State Listen | ? LocalPort -in 6768,6769`

---

## 六、进程环境

### 13. 多实例互相抢

反复 `start` 会留下多个 `Cavalry_ShortCut.exe`，各自有窗口、抢热键，表现为"有时能呼出有时不能"。
**修法**：`CreateMutexW(L"CavalryShortCut_Single")` 单实例；`--show` 时向已有实例 `PostMessage(WM_APP+1)`。

### 14. 运行的是旧 exe

改了代码/配置但没重启进程，所有现象都对不上。
**判据**：看日志文件是否更新；对比 exe 的 `LastWriteTime` 与进程 `StartTime`。

### 15. 批量文本替换把代码插进了**别的函数** ⚠️ 踩过（很隐蔽）

现象：搜索结果每行左边出现 `{}`，压在序号 1/2/3 上；圆盘却一切正常。

原因：给"扇区加图标"时，用 `if (g_slots[i].kind == L"layer") DrawIcon(...)` 这行做锚点批量插入，
但 **`DrawResults()` 里有一模一样的语句**（搜索结果也画图标）→ 代码被同时插进两处：

- 搜索结果行里用的是 `g_slots[i]`（扇区数组），对列表项是无效索引；
- 画的是 MDL2 字形 `\uE943`，在不匹配上下文渲染成 `{}`；
- 位置又是序号的位置，于是"`{}` 和数字编号挤在一起"。

**教训（以后照做）**：

1. 批量替换前，先 `grep` 该锚点**出现几次**；出现 >1 次就必须带**函数名/行号范围**定位；
2. 改完立刻用 `Select-String` 确认**只多了一处**，不是两处；
3. 症状是"局部出现莫名符号/图形"时，优先怀疑**绘制代码被放到了另一个 Draw 函数里**；
4. 同一个视觉元素（图标、标签、键帽）如果要在圆盘和列表里都出现，**抽成公共函数**，别复制两段。

### 16. 行高常量不同步（26 ↔ 30）⚠️ 踩过两次

现象：
- 搜索结果/设置页的**文字和图标贴着行顶部**，下方空一截；
- 或者**右键列表某一项直接关闭面板**，不弹菜单。

原因：把行高从 26 改成 30 时，只改了**一部分**：

```cpp
RowRect:      r.bottom = r.top + 14;   // ← 被别的替换误伤
VisibleRows:  h / 30;                  // ← 已改
右键命中:      (rp.y - fr.top) / 26;   // ← 没改
```

- 行矩形高度 < 行距 → `DT_VCENTER` 只在那条矮矩形里居中 → 文字贴顶；
- 除数和行距不一致 → 算出的行号偏大 → 越界 → 落到"点击面板外就关闭"的分支。

**修法**：容量、行矩形、命中计算**必须同一个常量**：

```cpp
static const int kRowH = 30;
VisibleRows():  h / kRowH
RowRect(i):     r.bottom = r.top + kRowH
命中:            (y - r.top) / kRowH   // 再用 RowRect(row) 复核 x/y
```

**教训**：改行高/字号这类"布局常量"时，全局搜一次该数字（26、30），确认所有引用点都改了。

### 17. 子窗口（输入框/滚动条）忘了加偏移 ⚠️ 踩过

现象：搜索面板里**输入框跑到窗口左上角**，看起来像"搜索栏和面板分离"，面板里那格是空的。

原因：热盒为了扩大鼠标捕获区，把整个窗口向外扩了 `kPadX / kPadY`，界面内容绘制时平移了，但 `g_edit`、`g_scrollbar` 是**子窗口**，用客户区坐标定位，没有跟着平移：

```cpp
SetWindowPos(g_edit, HWND_TOP, ir.left + 8, ir.top + 6, ...);            // 错：少了 pad
SetWindowPos(g_edit, HWND_TOP, kPadX + ir.left + 8, kPadY + ir.top + 6, ...);  // 对
```

**教训**：新增"整体偏移/缩放"时，**逐个检查子窗口、GDI 坐标、命中测试**三类消费者，它们不共享绘制坐标系。

### 18. 同一位置有多条坐标来源 ⚠️ 踩过（表现为"疯狂切换"）

现象：鼠标移动时，扇区高亮在**左上和右侧之间疯狂跳**。

原因：悬停判定有两条路径——

| 路径 | 数据来源 | 是否减了 pad |
|---|---|---|
| `WM_MOUSEMOVE` | 消息里的客户端坐标 | ✅ 入口统一减 |
| 5ms 定时器 | `GetCursorPos` + `ScreenToClient` | ❌ 忘了减 |

同一个鼠标位置被算成两个不同的点，两条路径每 5ms 互相覆盖 `g_hover`。

**修法**：所有"屏幕 → 内容坐标"的转换集中到一处（入口重写 `lParam`），`GetCursorPos` 的地方逐个补减。

**教训**：坐标转换只要出现第二条路径，就一定会不同步；**宁可在入口转换一次，也不要每个处理函数各转一次**。

### 19. 用"顶层透明窗"当输入层 → 卡死/漏斗 ⚠️ 踩过（严重）

现象：热盒 UI 不显示、鼠标一直是**漏斗**、但热键和执行逻辑仍然正常。

原因：为了在"透明区域也能滚轮/点击"，加了一个 `WS_EX_TOPMOST + LWA_ALPHA(alpha=1)` 的全屏/大尺寸窗口，并把鼠标消息**转发**给主窗口。它和主窗口互相触发重绘/光标处理，进入忙状态。

**修法（正确做法）**：不要第二个窗口。把主窗口改成**逐像素 alpha**：

```cpp
// 32 位 DIB 绘制 → 纯黑像素设 alpha=1（不可见但可点），其它 alpha=255
px[i] = (c == 0) ? 0x01000000u : (c | 0xFF000000u);
UpdateLayeredWindow(hwnd, screen, &dst, &sz, mem, &src, 0, &blend, ULW_ALPHA);
```

同时**去掉**原来的颜色键 `SetLayeredWindowAttributes`（两者互斥）。

**教训**：
- 需要"看不见但能收鼠标"的区域，用 `alpha=1` 的单窗口方案，不要叠窗口转发消息；
- 任何"窗口套窗口 + 消息转发"的设计都要先考虑**重入**与**忙循环**；
- 这类问题一旦出现，用户侧表现为"程序卡住"，优先**回滚并保留旧版可运行状态**，再换方案。

---

## 七、30 秒排查清单（按顺序）

1. `Get-Process Cavalry_ShortCut` —— 进程在不在？（不在就启动）
2. 日志最后几行：
   - `bridge ok` / `not connected`
   - `hotkey off -> ok`
3. `%TEMP%\CavalryShortCut.show` 的**修改时间**是否在你按下快捷键时更新？
   - 不更新 → 脚本没跑（见 §二：文件名/目录/重启）
   - 更新 → 看日志有没有 `trigger hit` + `after show`
4. 有 `trigger hit` 但无 `after show` → 触发器代码没走 `ShowAtCursor`（§四.7）
5. 有 `after show: vis=1` 但屏幕看不到 → 窗口被遮挡（§四.8）或位置在屏幕外
6. 能显示但点扇区无效 → 看日志是否 `Read the preamble`（§五.11）
7. 全都不对 → 换一个**全新的** exe 重启，并用 `--show` 直接启动验证一次

---

## 八、一句话经验

> **`IsWindowVisible` 不等于"用户看得见"。**
> 判断"窗口是否真的显示在最前"要用 `WindowFromPoint` / z 序检查，而不是可见性 API。
>
> **任何"按需注册 / 自动关闭"的实时监测，都会引入时序类 bug。**
> 能用手动关闭 + 简单触发链解决的，不要加监测。
