// Add Layers — 浏览式面板（从 Scripts 菜单打开）
//
// ⚠️ 这个脚本需要「脚本窗口」上下文（完整 ui 模块）：
//    从 Window > Scripts 菜单点开才行。
//    快捷键触发时 Cavalry 给的是「无 UI 上下文」（ui 只有 Modal），
//    List / setTitle 都不存在 —— 那种场合请绑 "Add Layers (quick).js"。
//
// 用法：Window > Scripts > Add Layers → 搜索 / 双击新建。
// 建议：拖到侧边停靠，当常驻面板。

(function () {
    if (typeof ui.List !== "function") {
        console.log("Add Layers: 当前是无 UI 上下文（多半是快捷键触发）。请从 Scripts 菜单打开，或改用 Add Layers (quick).js。");
        try {
            if (typeof ui.Modal !== "undefined") {
                (new ui.Modal()).showMessage("Add Layers 面板需要从 Scripts 菜单打开（快捷键上下文没有 UI 模块）。");
            }
        } catch (e) {}
        return;
    }

    ui.setTitle("Add Layers");

    const ICON_BASE = api.getAppAssetsPath() + "/Icons/nodes/";

    function iconFor(type) {
        const candidates = [type, String(type).replace(/^[^:]+::/, "")];
        for (let i = 0; i < candidates.length; i++) {
            const p = ICON_BASE + candidates[i] + ".png";
            try { if (api.isFile(p)) return p; } catch (e) {}
        }
        return "";
    }

    const allTypes = api.getAllLayerTypes(true);
    const blocked = ["compNode", "keyframeLayer", "renderQueue", "audioTrack", "palette"];

    const rows = allTypes
        .filter(function (t) { return blocked.indexOf(t.type) === -1; })
        .sort(function (a, b) { return a.name.localeCompare(b.name); })
        .map(function (t) {
            return { uuid: t.type, label: t.name, tooltip: t.type, icon: iconFor(t.type) };
        });

    const list = new ui.List();
    list.showSearchBar(true);
    list.setPlaceholder("No matching layers");
    list.setSelectionMode("single");
    list.setRowsRenamable(false);
    list.setRowsDeletable(false);
    list.setRowsReorderable(false);
    list.setModel(rows);

    function create(type) {
        try {
            const id = api.create(type);
            if (id) {
                api.select([id]);
                console.log("Add Layers: created " + type + " -> " + id);
            }
        } catch (e) {
            console.log("Add Layers: 创建失败 " + type + " -> " + e);
        }
        try { list.setSearchFilter(""); } catch (e) {}
    }

    list.onRowDoubleClicked = function (type) { create(type); };

    const createButton = new ui.Button("Create selected");
    createButton.onClick = function () {
        const sel = list.getSelection();
        if (sel && sel.length) create(sel[0]);
        else console.log("Add Layers: 先在列表里选一项");
    };

    const layout = new ui.VLayout();
    layout.add(list);
    layout.add(createButton);

    ui.add(layout);
    ui.setMinimumWidth(360);
    ui.setMinimumHeight(480);
    ui.show();
})();
