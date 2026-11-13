# Dalimao · Cavalry ShortCuts

给 [Cavalry](https://cavalry.studio)（2.8.1）补几个官方没有的能力：一个**外部热盒**，和几个**脚本**。

不注入、不修改 Cavalry 本体——全部通过 Cavalry 自带的 **MCP 服务器**（默认 `127.0.0.1:6768`）驱动。

```
hotbox.exe ──HTTP──▶ cavalry_bridge.mjs (127.0.0.1:6769) ──SSE/MCP──▶ Cavalry
```

## 目录

| 路径 | 内容 |
|---|---|
| `hotbox/` | Win32 C++ 热盒：三个按钮一键建图层，`Alt+H` 全局呼出/隐藏 |
| `bridge/` | Node 写的 MCP 桥，把 Cavalry 的 SSE 接口变成普通 HTTP |
| `scripts/` | Cavalry 脚本，放进 `%APPDATA%\Cavalry\Scripts` 即可 |

## 快速开始

### 1. 启动桥（必须先跑）

```bash
cd bridge
node cavalry_bridge.mjs
```

- 监听 `127.0.0.1:6769`；
- 自动连 Cavalry 的 `http://[::1]:6768/sse`；
- 首次连接时 Cavalry 会弹一次「Allow」，之后按指纹记住（指纹存在 `~/.cavalry-mcp/fingerprint-*`）；
- 日志写到同目录的 `cavalry_bridge.log`。

### 2. 跑热盒

双击 `hotbox/hotbox.exe`：

| 按钮 | 动作 |
|---|---|
| Duplicator | `api.create("duplicator")` |
| Number Range | `api.create("numberRange")` |
| Math | `api.create("math")` |

`Alt+H` 全局呼出/隐藏，`Esc` 收起。

重新编译：双击 `hotbox/build.bat`（需要 VS2019 BuildTools 的 MSVC）。

### 3. 装脚本

把 `scripts/*.js` 复制到：

```
%APPDATA%\Cavalry\Scripts
```

然后从 Cavalry 顶部菜单 **Scripts** 里点开。**新脚本要重启一次 Cavalry** 才会出现在 Shortcut Manager 里。

| 脚本 | 说明 |
|---|---|
| `Add Layers.js` | 节点搜索面板：280 个图层类型 + 图标 + 搜索框，双击新建（从 Scripts 菜单打开）；建议拖到侧边停靠 |
| `Cut Selection.js` | Ctrl+X 剪切：序列化选中图层 → 写入系统剪贴板 → 删除 |
| `Cut Selection (with inputs).js` | 同上，但连上游输入图层一起打包（Duplicator 会带上它的输入形状） |
| `Paste Cut.js` | 粘贴 Cut 出来的内容（建议绑 Ctrl+Shift+V，别占 Ctrl+V） |

## 桥的 HTTP 接口

```bash
# 状态
curl http://127.0.0.1:6769/status

# 调 MCP 工具
curl -X POST http://127.0.0.1:6769/call \
     -H "Content-Type: application/json" \
     -d '{"name":"execute_script","args":{"script":"api.create(\"duplicator\")"}}'

# 任意 JSON-RPC 方法
curl "http://127.0.0.1:6769/raw?method=tools/list"
```

## 踩坑记录（写给以后的我）

Cavalry 2.8.1 的脚本层有几处和官方文档不一致，都实测过：

1. **MCP 只认 64 位十六进制指纹**，短字符串直接 403；缺了则 401 或挂住不回。
2. **`initialize` 只接受 `2025-11-25`** 协议版本。
3. **SSE 用 `\r\n` 分帧**，按 `\n\n` 切事件会一个都收不到。
4. **`api.serialise(ids, true)` 会连带上游输入图层**——剪一个 Duplicator 会多剪一个输入形状；只要选中项用 `false`。
5. **快捷键触发的脚本跑在「无 UI 上下文」**：`ui` 只剩 `Modal`，`List`/`setTitle` 都没有；面板类脚本必须从 Scripts 菜单打开。
6. **脚本宿主窗口无法关闭**（`ui` 没有 close；`Container.close()` 只关次窗口；`showAsPopover()` 弹不出来）。
7. **`ui.List` / `ui.LineEdit` 都没有 focus 接口**，无法把焦点放进搜索框。

## 许可

自用为主，代码随便拿去改。Cavalry 是 Scene Group Ltd. 的商业软件，本项目不改它，只通过它开放的 MCP 接口通信。
