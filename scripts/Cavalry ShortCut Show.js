// Cavalry ShortCut caller
// Bind this in Window > Shortcut Manager (search "Cavalry ShortCut").
//
// Writes a trigger file to %TEMP%\CavalryShortCut.show.
// The running Cavalry_ShortCut.exe watches it and shows the pie.
// No global hotkey is registered, so no key is stolen from other apps.

(function () {
    try {
        var temp = api.getTempFolder();
        var path = temp + "/CavalryShortCut.show";
        var ok = api.writeToFile(path, "show " + Date.now(), true);
        console.log("Cavalry ShortCut trigger -> " + path + " ok=" + ok);
    } catch (e) {
        console.log("Cavalry ShortCut trigger failed: " + e);
    }
})();
