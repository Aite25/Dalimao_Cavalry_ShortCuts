// name: 设入点
// key: [
// Set In Point at playhead — content shifts so the layer starts at the playhead
(function () {
    var f = api.getFrame();
    var sel = api.getSelection();
    for (var i = 0; i < sel.length; i++) {
        var oldIn = api.getInFrame(sel[i]);
        if (oldIn !== f) api.offsetLayerTime(sel[i], f - oldIn);
        api.setInFrame(sel[i], f);
    }
    console.log("setIn " + sel.length + " @ " + f);
})();
