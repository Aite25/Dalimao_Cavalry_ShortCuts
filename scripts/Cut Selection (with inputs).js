// Cut Selection (with inputs) — 版本 B：连「上游输入图层」一起剪切
//
// 和 "Cut Selection.js" 的区别只在一个开关：
//   WITH_INPUTS = true  → 把该图层依赖的输入图层一起打包进剪贴板
//   WITH_INPUTS = false → 只剪切选中项自己（另一个文件）
//
// 什么时候用这个版本：
//   - 你想把一整套「Duplicator + 它的输入形状」整体搬到别处 / 别的场景；
//   - 目标场景里没有那些输入层。
// 代价：粘贴时会连同输入图层一起新建（剪一个 Duplicator，粘贴出 Duplicator + Rectangle）。
//
// 安装：放进 Cavalry 的 Scripts 目录（和 "Cut Selection.js" 同级）
// 绑键：Window > Shortcut Manager → "Cut Selection (with inputs)" → 建议 Ctrl+Shift+X
// 粘贴：同一个 "Paste Cut" 脚本即可（两种剪切的剪贴板格式一样）

(function () {
    const WITH_INPUTS = true;
    const LABEL = "Cut Selection (with inputs)";

    const selection = api.getSelection(true);

    if (selection.length === 0) {
        console.log(LABEL + ": 没有选中任何图层，什么也没做。");
        return;
    }

    const chosen = new Set(selection);

    // 只处理"最上层"的选中项。
    const topMost = selection.filter(function (id) {
        let parent = api.getParent(id);
        while (parent) {
            if (chosen.has(parent)) return false;
            parent = api.getParent(parent);
        }
        return true;
    });

    const editable = topMost.filter(function (id) {
        return api.getLayerType(id) !== "compNode";
    });

    if (editable.length === 0) {
        console.log(LABEL + ": 选中的只有合成节点，跳过。");
        return;
    }

    const payload = api.serialise(editable, WITH_INPUTS);
    api.setClipboardText(payload);

    const readBack = api.getClipboardText();
    if (readBack !== payload) {
        console.log(LABEL + ": 剪贴板写入失败，已取消，没有删除任何图层。");
        return;
    }

    for (let i = 0; i < editable.length; i++) {
        api.deleteLayer(editable[i]);
    }

    const after = api.getAllSceneLayers();
    const failed = editable.filter(function (id) {
        return after.indexOf(id) !== -1;
    });

    console.log(
        LABEL + ": 已剪切 " + (editable.length - failed.length) + " 个图层（含输入）" +
        "（" + payload.length + " 字符已放进剪贴板，用 Paste Cut 贴回）" +
        (failed.length ? "，失败 " + failed.length + " 个：" + failed.join(", ") : "")
    );
})();
