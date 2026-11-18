// name: 出点=当前帧（裁剪尾部）
// key: Alt+]
// Trim out point to playhead (content stays)
(function () {
    var f = api.getFrame();
    var sel = api.getSelection();
    for (var i = 0; i < sel.length; i++) api.setOutFrame(sel[i], f);
})();