# Dalimao Cavalry ShortCuts — Cavalry 径向热盒

给 **Cavalry 2.8.x** 用的外挂快捷面板：按 `Alt+Q` 在鼠标位置弹出一个 **8 扇区径向菜单**，
点一下就在 Cavalry 里执行脚本或新建图层。灵感与配色参考 AE 插件 `DalimaoShortcuts`。

![界面](#) <!-- 可放截图 -->

---

## 功能

| 能力 | 说明 |
|---|---|
| 径向圆盘 | 8 个方向扇区（N/NE/E/SE/S/SW/W/NW）+ 中心圆；鼠标离开中心即按方向高亮 |
| 透明浮层 | 背景色键透明，空白处鼠标穿透，不挡操作 |
| 确认方式 | 点扇区 / 按扇区热键（`1`~`8`）/ 按 `` ` `` 或 **空格** |
| 搜索菜单 | `Tab` 或直接按字母进入；搜索 **280 个图层类型 + 脚本**，支持分类页签 |
| 原生输入框 | `Ctrl+A/C/X/V`、鼠标拖选、光标闪烁、Backspace 均为系统原生行为 |
| 快捷键 | 结果前 6 项显示序号，`Ctrl+1`~`Ctrl+6` 直接执行 |
| 自绘滚动条 | 深色细轨 + 圆角滑块，滚轮 / 拖动 / 点轨均可 |
| 设置菜单 | `F2` 或齿轮图标；左侧圆盘预览 + 右侧绑定列表，写入 `Cavalry_ShortCut.json` |
| 热更新 | `actions/*.js` 在**点击时**才读取，改完存盘立即生效，不用重启 |
| 可拖动/缩放 | 中键拖动移动；右/下边缘拖动改变面板尺寸（仅搜索/设置页） |
| 托盘无关 | 右键热盒直接隐藏；不需托盘图标 |

---

## 安装

### 1. 准备

- Windows 10 / 11
- Cavalry 2.8.x（需在 `Preferences → Enable MCP Server` 打开，默认端口 6768）
- Node.js（给桥用；如果只用热盒、不用桥也可以跳过，但"执行脚本"需要它）

### 2. 放置文件

把仓库里的 `Cavalry_ShortCut\` 整个文件夹放到你喜欢的目录，例如：

```
D:\Tools\Cavalry_ShortCut\
```

### 3. 启动桥（可选但推荐）

热盒执行脚本是通过桥转发到 Cavalry 的 MCP：

```bat
bridge\start-bridge.cmd
```

或者热盒启动时会自动检测 `127.0.0.1:6769`，没在跑就自己拉起 `node cavalry_bridge.mjs`
（需要 `cavalry_bridge.mjs` 在 exe 同目录或其上一级）。

### 4. 启动热盒

双击 `Cavalry_ShortCut.exe`。首次运行会自动生成：

```
Cavalry_ShortCut.ini    ← 呼出热键、输入法开关
Cavalry_ShortCut.json   ← 8 个扇区的绑定
```

### 5. 开机自启（可选）

把 `Cavalry_ShortCut.exe` 的快捷方式放进：

```
%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\
```

---

## 使用

### 呼出

| 按键 | 作用 |
|---|---|
| `Alt+Q` | 在鼠标位置呼出 / 收起（可在 `.ini` 改） |

### 圆盘

| 操作 | 效果 |
|---|---|
| 鼠标移出中心圆 | 按方向高亮最近扇区，中心画蓝色弧线指向它 |
| 左键点扇区 | 执行 |
| 按扇区热键（默认 `1`~`8`） | 执行 |
| `` ` `` 或 **空格** | 执行当前高亮项 |
| 点中心圆 / `Esc` | 收起 |
| 右键 | 直接隐藏 |
| 中键拖动 | 移动窗口 |

### 搜索

| 按键 | 作用 |
|---|---|
| `Tab` 或直接按字母 | 进入搜索（字母会作为首个字符写入输入框） |
| `Ctrl+1`~`Ctrl+6` | 执行当前可见的第 N 项 |
| `Tab` / `Shift+Tab` | 切换分类页签（全部/脚本/形状/行为/工具/效果） |
| `↑` `↓` / `Enter` | 选择 / 执行 |
| `Esc` | 返回圆盘 |

### 设置

| 按键 | 作用 |
|---|---|
| `F2` 或齿轮图标 | 打开设置 |
| 点任意一行 / `Enter` | 为该扇区选择绑定（进入搜索，选中即绑定） |
| `Delete` | 清除该扇区 |
| 右上角 × | 关闭设置 |

---

## 配置

### `Cavalry_ShortCut.ini`

```ini
; 呼出热键（支持 Ctrl/Alt/Shift/Win + A-Z / 0-9 / F1-F24）
hotkey=Alt+Q

; ime=off 输入框禁用输入法（英文字母直接输入，推荐）
; ime=on  输入框启用输入法（可打拼音搜中文，但英文会被合成）
ime=off
```

> ⚠️ 只用 `Shift` 的组合（如 `Shift+A`）和**单键**（如 `` ` ``）Windows 不会派发全局热键，不要用。

### `Cavalry_ShortCut.json`

```json
{
  "sectors": [
    {"name":"Duplicator", "kind":"script", "value":"Duplicator.js", "key":"1"},
    {"name":"Rectangle",  "kind":"layer",  "value":"rectangle",     "key":"4"}
  ]
}
```

- `kind: "script"` → 执行 `actions/` 下对应的脚本文件
- `kind: "layer"` → 在 Cavalry 里新建该类型图层
- `key` → 该扇区的热键（单字符）

### 自定义动作

在 `actions\` 里放 `.js` 文件即可，文件名就是搜索里显示的名字：

```js
(function () {
    const rect = api.primitive("rectangle", "Rect");
    api.set(rect, { "material.materialColor": "#4D9FEC" });
    api.select([rect]);
})();
```

文件在**每次点击时**读取，改完存盘即生效。

---

## Cavalry 里的脚本（可选）

`scripts\` 里是几个可以直接放进 `%APPDATA%\Cavalry\Scripts\` 的脚本：

| 脚本 | 用途 |
|---|---|
| `Add Layers.js` | 带分类页签的图层浏览器（替代 `Ctrl+.`） |
| `Cut Selection.js` | `Ctrl+X` 剪切（剪贴板里是 Cavalry 可粘贴的 JSON） |
| `Paste Cut.js` | 粘贴上面剪切的内容 |
| `Set Comp Background.js` | 把当前合成背景设为 `#101014` |

> 放进 `Scripts` 后需要**重启一次 Cavalry** 才会出现在 Shortcut Manager；
> 脚本越多，Cavalry 启动扫描越慢，按需放。

---

## 已知限制

1. **必须在后台运行**：Cavalry 无法唤起外部程序，所以热盒得自己常驻（开机自启即可）；
2. **不能用 `Shift+字母` / 单键做全局热键**：Windows 不派发（已实测）；
3. **输入法与英文输入二选一**：`ime=on` 能打拼音搜中文，但英文会被合成；`ime=off` 反之；
4. 执行脚本依赖桥 + MCP，桥没起来时点扇区只会提示失败；
5. 托盘图标在部分环境下注册失败（`Shell_NamedIcon` 返回 `ACCESS_DENIED`），故用右键替代。

---

## 从源码构建

需要 Visual Studio 2019 BuildTools（MSVC）：

```bat
cd Cavalry_ShortCut
build.bat
```

或手动：

```bat
cl /nologo /EHsc /utf-8 Cavalry_ShortCut.cpp /Fe:Cavalry_ShortCut.exe ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup ^
   winhttp.lib user32.lib gdi32.lib shell32.lib imm32.lib comctl32.lib
```

---

## 开发笔记

踩过的坑与结论都写在 [`Cavalry_ShortCut/LESSONS.md`](Cavalry_ShortCut/LESSONS.md)：
热键注册窗口、透明窗口的点击穿透、`TranslateMessage` 与焦点时序、控制字符白名单、拖拽缩放的四个环节等。

---

## 致谢

界面与配色参考 AE 插件 **DalimaoShortcuts**（`config/hotbox_config.json`）。
