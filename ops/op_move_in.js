// name: 整体移动：入点对齐播放头（Move In）
// key: 
// Move In Point (like AE Dalimao "Move In Point")
// Moves ALL selected layers together so the EARLIEST layer start lands on the playhead.
(function () {
    var f = api.getFrame();
    var sel = api.getSelection();
    if (!sel.length) return;
    var minIn = null;
    for (var i = 0; i < sel.length; i++) {
        var v = api.getInFrame(sel[i]);
        if (minIn === null || v < minIn) minIn = v;
    }
    var delta = f - minIn;
    for (var j = 0; j < sel.length; j++) {
        if (delta !== 0) api.offsetLayerTime(sel[j], delta);
    }
    console.log("moveIn: " + sel.length + " layers, delta " + delta + " -> " + f);
})();
