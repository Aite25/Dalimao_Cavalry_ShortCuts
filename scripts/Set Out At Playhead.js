// Set Out Point at playhead (same as trimming to out point / Alt+])
// Put in %APPDATA%\Cavalry\Scripts\, restart Cavalry, bind a key if needed.

(function () {
    var f = api.getFrame();
    var sel = api.getSelection();
    for (var i = 0; i < sel.length; i++) {
        api.setOutFrame(sel[i], f);
    }
    console.log("setOut " + sel.length + " @ frame " + f);
})();
