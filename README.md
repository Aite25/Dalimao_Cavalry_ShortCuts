# Cavalry ShortCut（径向热盒）

参考 AE 插件 `DalimaoShortcuts` 的热盒：**8 方向扇区 + 中心圆**，在鼠标位置弹出，点扇区执行脚本。

```
            ┌──────────┐
            │ Duplicator ①│
   ┌────────┐    ┌────────┐
   │   ⑧   │ ⬤ │   Math ②│
   └────────┘    └────────┘
```

## 使用

| 操作 | 效果 |
|---|---|
| `Alt+Q` | 在鼠标位置弹出 / 再按收起（可在 `Cavalry_ShortCut.ini` 改） |
| 鼠标移出中心圆 | 对应扇区高亮，中心画蓝色弧线指向它 |
| 左键点扇区 | 执行该扇区的脚本，然后自动收起 |
| `1` ~ `8` | 直接执行第 N 个扇区 |
| 点中心圆 / `Esc` | 收起 |

快捷键规则（实测结论）：

- `Ctrl/Alt + 字母` 这类组合 → 走系统 `RegisterHotKey`，**正常**；
- **仅 `Shift + 字母`** → Windows 不派发 `WM_HOTKEY`，**不可用**；
- **单键**（如 `` ` ``）→ 同样不可靠，**不可用**。

## 动作 = 文件

`actions/*.js`，**文件名就是扇区名**，按文件名排序后依次填入：

| 顺序 | 方向 | 示例 |
|---|---|---|
| 1 | N | `Duplicator.js` |
| 2 | NE | `Math.js` |
| 3 | E | `Number Range.js` |
| 4 | SE | … |
| 5 | S | … |
| 6 | SW | … |
| 7 | W | … |
| 8 | NW | … |

- 最多 8 个（第 9 个以后不显示）；
- **点的时候才读文件** → 改完存盘即时生效，不用重启热盒，也不用重启 Cavalry；
- 文件内容就是普通 Cavalry 脚本（能用的 `api` 与 `%APPDATA%\Cavalry\Scripts` 里完全一样）。

## 弹窗方式

```
Cavalry_ShortCut.exe            # 常驻（Alt+Shift+A 呼出）
Cavalry_ShortCut.exe --show     # 让已在运行的实例在鼠标位置弹出（给 Cavalry 脚本用）
```

Cavalry 里的桥接脚本 `Cavalry ShortCut Show.js` 就是调用 `--show`，可以绑到 Cavalry 自己的快捷键上。

## 配置 `Cavalry_ShortCut.ini`

```ini
hotkey=Shift+A
```

支持 `Ctrl/Alt/Shift/Win` + `A-Z / 0-9 / F1-F24`。

⚠️ 热键是**系统级独占**：热盒运行期间该组合会被截获，Cavalry 收不到。若与 Cavalry 冲突，改成 `Ctrl+Shift+H` 之类，或用脚本 `--show` 的方式。

## 实现说明

- Win32 + GDI 自绘，无外部依赖；配色取自 AE 版 `config/hotbox_config.json`：
  - 扇区 `#232323` / 悬停 `#4D4D4D` / 描边 `#5F5F5F`
  - 中心 `#262626` / 弧线 `#3877B5`
- 热键注册在 **HWND_MESSAGE 消息窗口**上（主窗口会隐藏，挂在主窗口上会丢热键）。
- 启动时若 `127.0.0.1:6769` 的 bridge 没在跑，会自动 `node cavalry_bridge.mjs` 拉起。

## 搜索菜单（Tab / 直接打字）

热盒弹出后：

- 按 `Tab` → 进入搜索；
- 或直接按一个**没有被扇区绑定**的字母 → 直接进入搜索并带上这个字母。

规则是「**绑定优先**」：如果某个扇区的热键设成了 `A`，那么按 `A` 执行那个扇区，不会进搜索。

搜索范围 = `actions/*.js`（脚本）+ 内置常用图层类型（Duplicator / Number Range / Math / Rectangle / Text Shape …）。

| 键 | 作用 |
|---|---|
| 输入 | 过滤（子串 + 首字母匹配，如 `dup` → Duplicator） |
| `↑` `↓` | 选择 |
| `Enter` / 点击 | 执行 |
| `Backspace` | 删字符 |
| `Esc` | 返回圆盘 |

## 设置菜单（F2 或右键 → 设置…）

列出 8 个扇区当前的绑定：

| 操作 | 效果 |
|---|---|
| 点击某一行 / `Enter` | 进入搜索，**选中即绑定**到该扇区 |
| `Delete` | 清除该扇区绑定 |
| `↑` `↓` | 移动 |
| `Esc` | 返回圆盘 |

绑定保存在 `Cavalry_ShortCut.json`：

```json
{
  "sectors": [
    {"name":"Duplicator", "kind":"script", "value":"Duplicator.js", "key":"1"},
    {"name":"Rectangle",  "kind":"layer",  "value":"rectangle",     "key":"4"}
  ]
}
```

- `kind: "script"` → 执行 `actions/` 下的脚本文件；
- `kind: "layer"` → 在 Cavalry 里 `api.create(图层类型)`。

> 输入法会被临时关闭（`ImmAssociateContext(hwnd, nullptr)`），否则字母会被中文输入法吃掉。
