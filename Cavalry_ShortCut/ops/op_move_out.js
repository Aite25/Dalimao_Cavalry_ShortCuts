// name: 整体移动：出点对齐播放头（Move Out）
// key: 
// Move Out Point (like AE Dalimao "Move Out Point")
// Moves ALL selected layers together so the LATEST layer end lands on the playhead.
(function () {
    var f = api.getFrame();
    var sel = api.getSelection();
    if (!sel.length) return;
    var maxOut = null;
    for (var i = 0; i < sel.length; i++) {
        var v = api.getOutFrame(sel[i]);
        if (maxOut === null || v > maxOut) maxOut = v;
    }
    var delta = f - maxOut;
    for (var j = 0; j < sel.length; j++) {
        if (delta !== 0) api.offsetLayerTime(sel[j], delta);
    }
    console.log("moveOut: " + sel.length + " layers, delta " + delta + " -> " + f);
})();
