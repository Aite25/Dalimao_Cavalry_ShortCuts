// Set Comp Background #101014 — 把当前合成的底色设成深灰
//
// 为什么需要这个脚本：
//   Preferences 里确实有 defaultCompBackground 这个键（我用 api.createComp 实测过：
//   先把它设成纯红，再新建合成，新合成依然是纯白），说明这一版**根本没用它**。
//   所以"新合成默认底色"没法通过偏好设置做到，只能事后改。
//
// 用法：
//   Scripts 菜单点一次 → 当前合成底色变 #101014；
//   想更快就绑个快捷键（新脚本要重启 Cavalry 才进 Shortcut Manager）。
//
// 改成别的颜色：把下面 COLOR 换成任意 hex。

(function () {
    const COLOR = "#101014";

    const comp = api.getActiveComp();
    if (!comp) {
        console.log("Set Comp Background: 没有激活的合成。");
        return;
    }

    api.set(comp, { backgroundColor: COLOR });
    console.log("Set Comp Background: " + api.getNiceName(comp) + " -> " + COLOR);
})();
