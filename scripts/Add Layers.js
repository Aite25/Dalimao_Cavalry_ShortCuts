// Add Layers — 浏览式面板（分类页签 + 分类缓存，v4）
//
// 必须从 Scripts 菜单打开（快捷键上下文没有完整 ui 模块）。
//
// 关于分类：api.getAllLayerTypes 只给 {name, type}，没有类别信息。
// 旧版（v3）每次打开都要临时建 280 个实例再删掉，副作用是：
// 有些类型带 setup（比如 cornerPin 会建一个 Group），删主层时会留下残渣，
// 所以你每跑一次就多一堆 Corner Pin。
//
// v4 的改法：
//   1. 分类结果缓存到脚本同目录的 add-layers-categories.json，只算一次；
//   2. 算的时候不是"删掉我建的那一个"，而是先记录全场图层，算完把**新增的全部删掉**，
//      这样 setup 生成的子层也清得干净；
//   3. 想重新分类：删掉那个 json 再打开面板。
//
// 用法：Window > Scripts > Add Layers → 顶部页签切换 → 搜索 / 双击新建。

(function () {
    if (typeof ui.List !== "function") {
        console.log("Add Layers: 当前是无 UI 上下文（多半是快捷键触发）。请从 Scripts 菜单打开。");
        try { if (typeof ui.Modal !== "undefined") (new ui.Modal()).showMessage("Add Layers 面板需要从 Scripts 菜单打开。"); } catch (e) {}
        return;
    }

    ui.setTitle("Add Layers");

    const ICON_BASE = api.getAppAssetsPath() + "/Icons/nodes/";
    const blocked = ["compNode", "keyframeLayer", "renderQueue", "audioTrack", "palette", "compositionReference"];
    const CACHE = ui.scriptLocation + "/add-layers-categories.json";

    function iconFor(type) {
        const candidates = [type, String(type).replace(/^[^:]+::/, "")];
        for (let i = 0; i < candidates.length; i++) {
            const p = ICON_BASE + candidates[i] + ".png";
            try { if (api.isFile(p)) return p; } catch (e) {}
        }
        return "";
    }

    function classify(type) {
        let supers = [];
        const before = api.getAllSceneLayers();
        try {
            const id = api.create(type);
            supers = api.getSuperTypes(id) || [];
        } catch (e) {
            supers = [];
        }
        // 把这次新建出来的一切（含 setup 生成的子层）全删掉
        api.getAllSceneLayers().forEach(function (id) {
            if (before.indexOf(id) === -1) {
                try { api.deleteLayer(id); } catch (e) {}
            }
        });
        const chain = supers.join(",").toLowerCase();
        if (chain.indexOf("filter") !== -1 || chain.indexOf("shader") !== -1) return "Effects";
        if (chain.indexOf("behaviour") !== -1) return "Behaviours";
        if (chain.indexOf("shape") !== -1 || chain.indexOf("drawable") !== -1) return "Shapes";
        if (/filter|shader/i.test(type)) return "Effects";
        return "Utilities";
    }

    const t0 = Date.now();
    const types = api.getAllLayerTypes(true).filter(function (t) {
        return blocked.indexOf(t.type) === -1;
    });

    // 读缓存；没有就现算并存下来
    let catMap = null;
    try {
        const raw = api.readFromFile(CACHE);
        if (raw && raw.trim().charAt(0) === "{") catMap = JSON.parse(raw);
    } catch (e) { catMap = null; }

    if (!catMap) {
        console.log("Add Layers: 首次分类（会临时建/删图层，一次性）…");
        catMap = {};
        types.forEach(function (t) { catMap[t.type] = classify(t.type); });
        try {
            api.writeToFile(CACHE, JSON.stringify(catMap, null, 1), true);
            console.log("Add Layers: 分类已缓存到 " + CACHE);
        } catch (e) {
            console.log("Add Layers: 缓存写入失败 -> " + e);
        }
    }

    const rows = types.map(function (t) {
        return {
            uuid: t.type,
            label: t.name + "  (" + t.type + ")",
            tooltip: t.type,
            icon: iconFor(t.type),
            category: catMap[t.type] || "Utilities",
        };
    }).sort(function (a, b) { return a.label.localeCompare(b.label); });

    console.log("Add Layers: 就绪，" + rows.length + " 个类型，用时 " + (Date.now() - t0) + "ms");

    function makeList(listRows) {
        const list = new ui.List();
        list.showSearchBar(true);
        list.setPlaceholder("No matching layers");
        list.setSelectionMode("single");
        list.setRowsRenamable(false);
        list.setRowsDeletable(false);
        list.setRowsReorderable(false);
        list.setModel(listRows);
        list.onRowDoubleClicked = function (type) {
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
        };
        return list;
    }

    function pageOf(listRows) {
        const layout = new ui.VLayout();
        layout.add(makeList(listRows));
        return layout;
    }

    const tabs = new ui.TabView();
    tabs.add("All", pageOf(rows));
    ["Shapes", "Behaviours", "Utilities", "Effects"].forEach(function (cat) {
        tabs.add(cat, pageOf(rows.filter(function (r) { return r.category === cat; })));
    });

    ui.add(tabs);
    ui.setMinimumWidth(380);
    ui.setMinimumHeight(520);
    ui.show();
})();
