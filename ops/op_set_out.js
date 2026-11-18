// name: 设出点（设为当前帧）
// key: ]
// Set Out Point at playhead — content shifts so the layer ends at the playhead
(function () {
    var f = api.getFrame();
    var sel = api.getSelection();
    for (var i = 0; i < sel.length; i++) {
        var oldOut = api.getOutFrame(sel[i]);
        if (oldOut !== f) api.offsetLayerTime(sel[i], f - oldOut);
        api.setOutFrame(sel[i], f);
    }
})();