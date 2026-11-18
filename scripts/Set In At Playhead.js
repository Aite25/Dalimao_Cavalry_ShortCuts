// Set In Point at playhead (same as trimming to in point / Alt+[)
// Put in %APPDATA%\Cavalry\Scripts\, restart Cavalry, bind a key if needed.

(function () {
    var f = api.getFrame();
    var sel = api.getSelection();
    for (var i = 0; i < sel.length; i++) {
        api.setInFrame(sel[i], f);
    }
    console.log("setIn " + sel.length + " @ frame " + f);
})();
