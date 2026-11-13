// Cut Selection — 版本 A：只剪切「选中的图层 + 它自己的子层」
//
// 和 "Cut Selection (with inputs).js" 的区别只在一个开关：
//   WITH_INPUTS = false → 不把上游输入图层打包进剪贴板（推荐，默认绑 Ctrl+X）
//   WITH_INPUTS = true  → 连上游输入一起打包（-- 见另一个文件）
//
// 为什么默认 false：Duplicator 的输入形状是"连接进来的独立图层"，不是子层。
// 打包输入的话，剪一个 Duplicator 粘贴时会多出一个 Rectangle。
//
// 安装：放进 Cavalry 的 Scripts 目录
//       Windows: %APPDATA%\Cavalry\Scripts
//       macOS:   ~/Library/Application Support/Cavalry/Scripts
// 绑键：Window > Shortcut Manager → "Cut Selection" → Ctrl+X
// 粘贴：用 "Paste Cut" 脚本（建议 Ctrl+Shift+V）

(function () {
    const WITH_INPUTS = false;

    const selection = api.getSelection(true);

    if (selection.length === 0) {
        console.log("Cut Selection: 没有选中任何图层，什么也没做。");
        return;
    }

    const chosen = new Set(selection);

    // 只处理"最上层"的选中项：祖先被选中时，后代已经包含在祖先的层级里。
    const topMost = selection.filter(function (id) {
        let parent = api.getParent(id);
        while (parent) {
            if (chosen.has(parent)) return false;
            parent = api.getParent(parent);
        }
        return true;
    });

    // 合成节点删不掉，也不该被序列化进剪贴板。
    const editable = topMost.filter(function (id) {
        return api.getLayerType(id) !== "compNode";
    });

    if (editable.length === 0) {
        console.log("Cut Selection: 选中的只有合成节点，跳过。");
        return;
    }

    // 1) 序列化
    const payload = api.serialise(editable, WITH_INPUTS);
    // 2) 写剪贴板
    api.setClipboardText(payload);
    // 3) 回读校验——写失败就绝不删，避免"剪没了"
    const readBack = api.getClipboardText();
    if (readBack !== payload) {
        console.log("Cut Selection: 剪贴板写入失败，已取消，没有删除任何图层。");
        return;
    }

    // 4) 删除
    for (let i = 0; i < editable.length; i++) {
        api.deleteLayer(editable[i]);
    }

    // 5) 对账（deleteLayer 没有返回值）
    const after = api.getAllSceneLayers();
    const failed = editable.filter(function (id) {
        return after.indexOf(id) !== -1;
    });

    console.log(
        "Cut Selection: 已剪切 " + (editable.length - failed.length) + " 个图层" +
        "（" + payload.length + " 字符已放进剪贴板，用 Paste Cut 贴回）" +
        (failed.length ? "，失败 " + failed.length + " 个：" + failed.join(", ") : "")
    );
})();
