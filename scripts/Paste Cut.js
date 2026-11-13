// Paste Cut — 配合 "Cut Selection" 用的粘贴端
//
// 背景：Cavalry 自带的 Ctrl+V 只认它自己的内部剪贴板，不读我们写进去的文本，
//       所以剪切出来的图层得用 api.deserialise() 贴回来。
//
// 安装：和 Cut Selection 放同一个 Scripts 目录。
// 绑键：Window > Shortcut Manager → 找 "Paste Cut" → 建议绑 Ctrl+Shift+V
//       （不要绑 Ctrl+V，否则会顶掉 Cavalry 原本的粘贴）。

(function () {
    const clip = api.getClipboardText();

    if (!clip || clip.trim() === "") {
        console.log("Paste Cut: 剪贴板是空的，没有可粘贴的内容。");
        return;
    }

    // 只接受 Cavalry 自己的序列化格式，避免把随便一段文本当图层数据塞进场景。
    let looksLikeCavalry = false;
    try {
        const parsed = JSON.parse(clip);
        looksLikeCavalry = !!parsed && typeof parsed === "object" &&
            (parsed.activeComp !== undefined || parsed.assets !== undefined);
    } catch (e) {
        looksLikeCavalry = false;
    }

    if (!looksLikeCavalry) {
        console.log("Paste Cut: 剪贴板里不是 Cavalry 图层数据（可能被别的复制操作覆盖了），已取消。");
        return;
    }

    const before = api.getAllSceneLayers().length;
    try {
        api.deserialise(clip);
    } catch (e) {
        console.log("Paste Cut: 反序列化失败：" + e);
        return;
    }
    const after = api.getAllSceneLayers().length;

    console.log("Paste Cut: 粘贴了 " + (after - before) + " 个图层（含子图层）。");
})();
