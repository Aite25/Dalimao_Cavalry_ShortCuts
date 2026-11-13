# Cavalry Hotbox — 第一步：能跑的最小骨架

## 这是什么

一个 **Win32 C++ 小程序**，窗口上有三个按钮（Duplicator / Number Range / Math），
点一下就让 Cavalry 建对应图层。它不碰 Cavalry 的进程，走的是 Cavalry 自己的 MCP 接口：

```
hotbox.exe ──HTTP──▶ cavalry_bridge.mjs (127.0.0.1:6769) ──MCP──▶ Cavalry
```

## 跑起来

1. **先启动桥**（Cavalry 得开着）：
   ```
   cd E:\dsh work\dsh_cavalry
   node cavalry_bridge.mjs
   ```
2. 双击 `hotbox-v2.exe`（v1 的 `hotbox.exe` 没热键）；
3. 点按钮 → Cavalry 场景里出现对应图层，窗口底部一行显示返回结果。

## 热键

| 键 | 作用 |
|---|---|
| **Alt + H** | 呼出 / 隐藏（全局，别的程序在前台也管用） |
| **Esc** | 收起窗口（不退出） |

- 热键用 `RegisterHotKey(MOD_ALT \| MOD_NOREPEAT, 'H')`，**全局只有一个程序能占用**；
  如果注册失败，启动时会弹提示（多半是别的程序占了 Alt+H）。
- 关掉窗口 = 退出程序，热键随之释放。

## 重新编译

```
build.bat
```

（用 VS2019 BuildTools 的 cl，`/SUBSYSTEM:WINDOWS` 所以不会带控制台窗口。）

## 代码里的关键点

| 位置 | 说明 |
|---|---|
| `PostJson()` | WinHTTP：连 127.0.0.1:6769，POST `/call`，读回响应正文 |
| `RunScript()` | 拼请求体 `{"name":"execute_script","args":{"script":"…"}}`，脚本里的引号要写成 `\"` |
| `WM_COMMAND` | 三个按钮 → 三个图层类型 |
| `WM_CTLCOLORSTATIC` / `WM_ERASEBKGND` | 深色底，浅色字 |

## 请求体长这样（可直接用 curl 验证）

```json
{"name":"execute_script","args":{"script":"api.create(\"duplicator\")"}}
```

```bash
curl -X POST http://127.0.0.1:6769/call -H "Content-Type: application/json" -d @body.json
```

## 下一步可以加什么

- 全局热键（`RegisterHotKey`）→ 随时呼出/隐藏；
- 九宫格 / 径向布局 + 图标（图标资源就在 `Cavalry\assets\Icons\nodes\*.png`）；
- 每个按钮映射一段更长的脚本（连线、设属性、批量改名都能写）；
- 读场景（`execute_script` 里 `console.log` 的结果会从 `/call` 的响应里回来）。
