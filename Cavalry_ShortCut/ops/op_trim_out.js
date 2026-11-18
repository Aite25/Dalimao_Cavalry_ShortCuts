// name: 裁剪出点
// key: Alt+]
// Trim out point to playhead (content stays)
(function () {
    var f = api.getFrame();
    var sel = api.getSelection();
    for (var i = 0; i < sel.length; i++) api.setOutFrame(sel[i], f);
})();
