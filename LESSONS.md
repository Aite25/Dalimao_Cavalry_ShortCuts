# Cavalry 热盒开发笔记（v6 前后踩过的坑）

> 这份文档记录 2026-11-16 ~ 11-17 之间，把"AE 热盒"移植成 **Cavalry 外部热盒**过程中
> 遇到的真问题、验证方式和结论。目的：下次改这个插件时不再重复踩坑。

---

## 0. 一句话结论

**热盒不是插件，是一个独立 Win32 程序**：它常驻后台、注册全局热键、自绘界面，
通过 `127.0.0.1:6769` 的 Node 桥调用 Cavalry 的 MCP（`execute_script`）。
Cavalry 本身**无法**唤起它，也无法提供"能自己关闭的窗口"——这是外部进程方案的必然结果。

---

## 1. 架构

```
Cavalry_ShortCut.exe ──HTTP──▶ cavalry_bridge.mjs (127.0.0.1:6769) ──MCP/SSE──▶ Cavalry (127.0.0.1:6768)
   │
   ├─ Cavalry_ShortCut.ini   呼出热键
   ├─ Cavalry_ShortCut.json  8 扇区绑定（script / layer）
   ├─ actions/*.js 动作脚本（点的时候才读 → 热更新）
   └─ layer-types.txt  280 个图层类型（从 Cavalry 一次性导出）
```

---

## 2. 热键：四个坑，一个比一个隐蔽

### 2.1 `RegisterHotKey` 挂在"会被隐藏的窗口"上，热键会失效

现象：第一次按能开，之后按没反应。

原因：热键最初注册在**主窗口**上，而热盒会 `ShowWindow(SW_HIDE)`。
隐藏之后 `WM_HOTKEY` 不再派发（实测日志里只有第一行）。

**修法**：注册到 `HWND_MESSAGE` 的**消息窗口**上——它天生不可见、永不销毁：

```cpp
HWND msgWnd = CreateWindowExW(0, CLASS, L"", 0, 0,0,0,0, HWND_MESSAGE, ...);
RegisterHotKey(msgWnd, 1, mods, vk);
```

### 2.2 `WM_HOTKEY` 派发给"注册它的窗口"，不是主窗口

注册在消息窗口上之后，`WM_HOTKEY` 也发给消息窗口（它是不可见的），
于是代码去"显示"那个看不见的窗口，主窗口纹丝不动。

**修法**：处理时统一转主窗口：

```cpp
case WM_HOTKEY:
    if (wp == 1) ToggleWindow(g_main ? g_main : hwnd);
```

日志症状：`WM_HOTKEY visible=0 → now visible=0`（怎么弄都还是 0）。

### 2.3 只有 `Shift` 的组合，Windows 不派发 `WM_HOTKEY`

`Shift+A`、`` ` ``（单键）**都收不到**；`Ctrl+Shift+H`、`Alt+Q` 正常。
低级键盘钩子（`WH_KEYBOARD_LL`）和 `GetAsyncKeyState` 轮询也没能稳定救回来
（钩子回调里做文件 I/O 超过 `LowLevelHooksTimeout` 会被系统摘掉）。

**结论**：只支持"带 Ctrl/Alt 的组合"。选键原则：

- 离左手近（`Alt+Q`、`Ctrl+Shift+H`）；
- 不要 `Alt+字母` 里和菜单助记冲突的（曾误触发 Cavalry 的 Help 菜单）；
- 不要 `Ctrl+Space` / `Shift+Space`（输入法切换）。

### 2.4 双通道重复触发

同时开 `RegisterHotKey` 和轮询时，一次按键被处理两遍（先开后关，看起来"没反应"）。

**修法**：注册成功就关掉轮询，二者只留一个。

---

## 3. 输入框：原生 `EDIT` 是对的选择，但要处理三件事

### 3.1 自绘输入框的连锁问题

自绘 + 手动 `WM_CHAR` 会导致：

- 进搜索时首个字母被插两次（`WM_KEYDOWN` 一次、`WM_CHAR` 一次）；
- `Backspace` 删两个；
- 没有选区、没有光标。

**改用原生 `EDIT`** 之后，`Ctrl+A/C/X/V`、鼠标拖选、光标闪烁全部由系统提供。

### 3.2 `TranslateMessage` 早于 `DispatchMessage`

```
GetMessage → TranslateMessage（此时就按"当前焦点"排队 WM_CHAR）→ DispatchMessage
```

所以：

1. 在 `WM_KEYDOWN` 里把焦点切到输入框；
2. 那条 `WM_CHAR` 仍然是发给**旧焦点窗口**（主窗口）的。

**修法**：主窗口收到 `WM_CHAR` 时**一律转交**给输入框：

```cpp
case WM_CHAR:
    if (g_mode == M_SEARCH && g_edit) {
        SendMessageW(g_edit, WM_CHAR, wp, lp);
        SendMessageW(g_edit, EM_SETSEL, -1, -1);   // 插入点放末尾
    }
    return 0;
```

> 曾经写成 `if (GetFocus() != g_edit)` 才转交 —— 结果因为焦点已经切过去，
> 条件恒为假，字符直接被丢弃（表现为"按 d 进了搜索但没有 d"）。
> **原则：字符能到主窗口，就说明它是冲我们来的。**

### 3.3 `Ctrl+A` 的"叮"

`Ctrl+A` 会额外产生控制字符 `0x01`，没人处理就触发系统提示音。
在输入框子类里吞掉：

```cpp
if (m == WM_CHAR && w <= 27) return 0;
if (m == WM_KEYDOWN && w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
    SendMessageW(h, EM_SETSEL, 0, -1);
    return 0;
}
```

### 3.4 输入法（IME）

`ImmAssociateContext(hwnd, nullptr)` 是**按窗口**生效的：

- 主窗口禁用 → 圆盘状态下字母干净地触发搜索；
- 输入框保留自己的 IME 上下文 → 搜索框里能打拼音。

> 想做到"按 d 同时被输入法截获 + 进搜索"是做不到的：IME 截获要求焦点窗口挂 IME，
> 而搜索触发依赖主窗口收 `WM_KEYDOWN`，二者互斥。


### 3.5 控制字符要"白名单"，不能按范围一刀切

为了消掉 `Ctrl+A` 的提示音，最初写的是：

```cpp
if (m == WM_CHAR && w <= 27) return 0;   // ← 把 Backspace 也吞了
```

结果 **Backspace 失效**（它的 `WM_CHAR` 是 8，正好在范围内）。

正确写法是放行编辑必需键：

```cpp
if (m == WM_CHAR && w < 32 && w != 8) return 0;
```

| 键 | WM_CHAR | 处理 |
|---|---|---|
| Backspace | 8 | **放行**（删除字符） |
| Tab | 9 | 放行（用于退出搜索） |
| Enter | 13 | 放行（执行选中项） |
| `Ctrl+A` 产生的 | 1 | 吞掉（避免系统提示音） |

> 经验：**过滤"控制字符"时先想清楚哪些是功能键**，再决定吞哪些。
---

## 4. 窗口外观

### 色键透明

```cpp
CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, ..., WS_POPUP, ...);
SetLayeredWindowAttributes(hwnd, RGB(0,0,0), 255, LWA_COLORKEY);
```

- 背景画纯黑 → 完全透明，**空白处鼠标可穿透**；
- **必须加 `WS_CLIPCHILDREN`**，否则父窗口的 `BitBlt` 会把子控件（输入框）涂掉。

### 置顶与失焦

- `WS_EX_TOPMOST` 会让它压在**所有**应用上（被嫌弃过），改成 `HWND_TOP`；
- 失焦自动隐藏，但 `WM_ACTIVATE` 在输入过程中可能抖动，会把状态复位 →
  更容易踩的坑是"每次按键都重新进搜索"。

---

## 5. 托盘图标：`Shell_NotifyIcon` 返回 5

```
tray icon handle=65581   ← 图标句柄有效
tray icon FAIL err=5     ← ERROR_ACCESS_DENIED
```

试过：标准结构体、`NOTIFYICON_VERSION_4`、窗口显示后注册、每秒重试 —— 全部失败。
同会话（SessionId=1）、Medium 完整性级别，排除会话/提权问题。

**结论**：环境限制，放弃托盘；用**右键菜单**（打开 actions、看日志、改 ini、退出）替代。
> 后来右键改成了"直接隐藏"，菜单改由 `F2` / 齿轮图标进入。

---

## 6. 改代码方式的血泪教训（比技术点更重要）

这一轮里，多次出现"以为改了、其实没改"：

- 用**字符串替换**打补丁，因为文件换行是 `\n` 而我写了 `\r\n`，替换静默失败；
- 用"锚点字符串"插入代码，锚点在多个函数里同名（`else if (g_mode == M_SEARCH)`），
  结果把代码插进了 `Paint()`，引发 `EndPaint` 重定义等一连串错误；
- 删除"多余行"时误删了成对的大括号，整个文件编译不过。

**现在的做法（必须坚持）**：

1. 改之前先 `Copy-Item Cavalry_ShortCut.cpp Cavalry_ShortCut.cpp.bak`；
2. 用**行号**或**整段重写**，不用模糊字符串锚点；
3. 改完立刻 `Select-String` 回读，确认代码真的写进去了；
4. 再编译；编译不过就回滚，不在坏文件上继续叠改；
5. 编译通过 → 本地 `git commit`（**不 push**）。

---

## 7. 排查方法论（这些手段是有效的）

| 手段 | 用途 |
|---|---|
| `Cavalry_ShortCut.log` 逐事件写入 | 判断"消息有没有到"（`WM_HOTKEY` / `WM_CHAR` / `focus`） |
| `PostMessage(hwnd, WM_KEYDOWN, ...)` | 定向测试某个分支，不受前台窗口干扰 |
| `EnumChildWindows` + `GetWindowRect` | 确认控件是否创建、位置尺寸对不对 |
| 全屏截图（`CopyFromScreen`） | 验证自绘 UI 的真实观感 |
| 查 `GetLastError()` | 例如 `Shell_NotifyIcon` 的 5 = ACCESS_DENIED |

> **反例**：用 `keybd_event` 做全局按键注入不可靠——按键会落到**前台应用**
> （曾把 `te` 打进用户的 QQ 窗口），也会被 IME 吞掉。定向 `PostMessage` 更可控。

---

## 8. 待办（下次继续）

1. ~~搜索滚动条~~ ✅ 已实现自绘滚动条（深色细轨 + 圆角滑块，拖动/点轨/滚轮）；遗留：`↑↓` 选中项自动滚入视野
2. 设置界面增加"扇区热键"列（现在只有方向 + 绑定名）；
3. 清理遗留的 `SetClipboardText` / `GetClipboardText`（原生控件已接管）；
4. 托盘图标（受环境限制，暂缓）。

---

## 9. 关键文件职责

| 文件 | 作用 |
|---|---|
| `Cavalry_ShortCut.cpp` | 全部逻辑：热键、绘制、搜索、设置、HTTP 调用桥 |
| `Cavalry_ShortCut.ini` | `hotkey=Alt+Q` |
| `Cavalry_ShortCut.json` | 8 扇区绑定（`kind`: `script` / `layer`） |
| `actions/*.js` | 动作脚本，点击时读取（热更新） |
| `layer-types.txt` | 280 个图层类型，`名称\t类型` |
| `README.md` | 使用说明 |
| `build.bat` | MSVC 编译 |

---

## 10. 一句话备忘

> **热键用"消息窗口 + Alt/Ctrl 组合"；输入用"原生 EDIT + 一律转交"；
> 改代码用"备份 + 行号修改 + 回读验证 + 本地提交"。**

---

## 11. 已验证可用的功能（2026-11-17）

| 功能 | 状态 |
|---|---|
| `Alt+Q` 呼出 / 收起（鼠标位置） | ✅ |
| 径向 8 扇区 + 中心圆 + 悬停蓝色弧线 | ✅ |
| 透明背景（色键），空白处鼠标穿透 | ✅ |
| 点扇区 / 扇区热键 / 搜索 `Enter` 执行 | ✅ |
| 按字母进搜索且字母落入输入框 | ✅ |
| 搜索：283 项（280 图层 + 脚本）、6 个分类页签 | ✅ |
| 输入框：`Ctrl+A/C/X/V`、鼠标拖选、光标闪烁、Backspace | ✅ |
| 中文输入法（`ime=off` 默认；`ime=on` 可打拼音） | ✅ |
| **自绘滚动条**（深色细轨 + 圆角滑块，拖动/点轨/滚轮） | ✅ |
| 右键直接隐藏 | ✅ |
| 中键拖动窗口 | ✅ |
| 设置（`F2` / 齿轮）：左预览 + 右绑定列表 → `Cavalry_ShortCut.json` | ✅ |
| 动作热更新（点击时才读 `actions/*.js`） | ✅ |

> 现在出问题时的第一步：看 `Cavalry_ShortCut.log` 最后几行的时间戳，
> 如果停留在几分钟前，说明**进程根本没在跑**（不是代码 bug）。
---

## 12. 透明窗口下的点击穿透（2026-11-17 补）

热盒用**色键透明**：

```cpp
SetLayeredWindowAttributes(hwnd, RGB(0,0,0), 255, LWA_COLORKEY);
```

含义不只是"看不见"，而是**纯黑像素的鼠标事件会穿透到下面的窗口**。因此：

> **任何需要点击的 UI（图标、拖拽条、按钮）都必须画在不透明像素上。**

这解释了为什么"拖拽带放在面板外面"怎么改都拖不动——那片区域是黑的，
点击直接落到 Cavalry 上，`WM_LBUTTONDOWN` 根本没到我们这。

正确做法：把拖拽条画成**面板内侧的一条不透明暗条**（`#2E2E32`），
判定范围也用面板内侧坐标（`pr.right - 14` / `pr.bottom - 14`）。

---

## 13. 拖拽缩放的四个必备环节

缺任何一个都会出现"点了没反应 / 松手还在拖 / 抖动"：

| 环节 | 代码 | 缺了会怎样 |
|---|---|---|
| 1. 命中判定 | `WM_LBUTTONDOWN` 里算 `edges`，`SetCapture` | 拖不动 |
| 2. 移动处理 | `WM_MOUSEMOVE` 里 `if (g_resizing) SetWindowPos(...)` | **光标变了但拖不动**（本轮真实故障） |
| 3. 松手复位 | `WM_LBUTTONUP` 里 `g_resizing=false; ReleaseCapture()` | **松手后还在拖** |
| 4. 光标反馈 | `WM_SETCURSOR` 返回 `IDC_SIZEWE/NS/NWSE` | 不知道能不能拖 |

另外两条容易忽略的：

- 缩放时要同步 `SyncEditFromMode()`（否则输入框溢出面板）和 `UpdateScrollInfo()`（否则滚动条长度不对）；
- 只在**面板模式**（搜索/设置）启用缩放，圆盘模式下不画拖拽条、不做判定，否则鼠标在圆盘上移动会反复命中/不命中，表现为"疯狂闪烁"。

---

## 14. 这一轮最常犯的错：补丁没落地

统计下来，**同一个功能的代码我写了 3~4 次才真正生效**，原因全是"以为改了"：

- 用 `$t.Replace("旧串", "新串")`，因为换行 `\n`/`\r\n` 或空格不一致 → **静默不匹配**；
- 用"行号区间替换"时算错区间 → 把 `if (edges) {` 这类括号行覆盖掉 → 编译错误；
- 改动后没确认 → 编译过了（因为改动没生效），却以为功能已实现。

**现在的固定流程**：

1. 改之前 `git` 已提交，坏了能回滚；
2. 用**行号插入/整段替换**，不用模糊字符串；
3. 改完 `Select-String` **计数验证关键行**（如 `mousemove-resize=1`），再编译；
4. **编译通过才 `git commit`**（这一轮曾在编译失败时提交，产生了两个坏提交，后来回滚）；
5. 每轮结束在 `Cavalry_ShortCut.log` 和 UI 上各确认一次。
