// name: 裁剪入点
// key: Alt+[
// Trim in point to playhead (content stays)
(function () {
    var f = api.getFrame();
    var sel = api.getSelection();
    for (var i = 0; i < sel.length; i++) api.setInFrame(sel[i], f);
})();
