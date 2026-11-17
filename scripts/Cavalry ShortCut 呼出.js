// Cavalry ShortCut 呼出器 —— 在 Cavalry 里按快捷键呼出热盒
//
// 原理：写一个"信号文件"到 %TEMP%\CavalryShortCut.show，
//       已经在后台运行的 Cavalry_ShortCut.exe 每 25ms 检查一次，
//       发现文件就删掉它并把热盒弹到鼠标位置。
//
// 前置：Cavalry_ShortCut.exe 必须在运行（已设置开机自启）。
//       如果它没在跑，这个脚本不会有任何效果——因为 Cavalry 2.8.1
//       没有"启动外部程序"的 API（api.runDetachedProcess 文档里有，运行时不存​在）。
//
// 安装：放进 %APPDATA%\Cavalry\Scripts\，重启 Cavalry，
//       然后在 Shortcut Manager 里给它绑键。

(function () {
    var path = api.getTempFolder() + "/CavalryShortCut.show";
    api.writeToFile(path, String(Date.now()), true);
    console.log("Cavalry ShortCut: 已发送呼出信号");
})();