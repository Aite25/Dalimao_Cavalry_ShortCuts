// name: 跳到图层出点
// key: End
(function () {
    var sel = api.getSelection();
    if (!sel.length) return;
    api.setFrame(api.getOutFrame(sel[sel.length - 1]));
})();
