// 建一个「看得见」的 Duplicator：矩形 × 4（Step 分布）
(function () {
    const rect = api.primitive("rectangle", "Rect");
    try { api.set(rect, { "material.materialColor": "#4D9FEC" }); } catch (e) {}
    try { api.set(rect, { "generator.dimensions": { x: 100, y: 100 } }); } catch (e) {}

    const dup = api.create("duplicator");
    api.connect(rect, "id", dup, "shapes");
    api.parent(rect, dup);

    try { api.set(dup, { "generator.count.x": 4, "generator.count.y": 1 }); } catch (e) {}
    try { api.set(dup, { "generator.distributionMode": 1 }); } catch (e) {}   // 1 = Step（按固定步距）
    try { api.set(dup, { "generator.size.x": 140 }); } catch (e) {}

    api.select([dup]);
    console.log("hotbox: duplicator built (" + dup + ")");
})();