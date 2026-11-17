# Cavalry_ShortCut 看护脚本（PowerShell 版）
#
# 作用：检测 Cavalry_ShortCut.exe 是否在运行，没有就静默启动它。
#
# 用法：
#   1) 单次检查（适合任务计划程序每分钟跑一次）
#        powershell -NoProfile -ExecutionPolicy Bypass -File Ensure-ShortCut.ps1
#   2) 常驻看护（本窗口保持运行，每 10 秒检查一次）
#        powershell -NoProfile -ExecutionPolicy Bypass -File Ensure-ShortCut.ps1 -Watch
#   3) 顺带注册成"登录时启动"的任务（需要管理员权限）
#        powershell -NoProfile -ExecutionPolicy Bypass -File Ensure-ShortCut.ps1 -InstallTask
#
param(
    [switch]$Watch,
    [int]$IntervalSeconds = 10,
    [switch]$InstallTask
)

$ErrorActionPreference = 'Stop'
$exe  = Join-Path $PSScriptRoot 'Cavalry_ShortCut.exe'
$name = 'Cavalry_ShortCut'

function Test-Running {
    return [bool](Get-Process -Name $name -ErrorAction SilentlyContinue)
}

function Start-ShortCut {
    if (-not (Test-Path $exe)) {
        Write-Error "找不到程序：$exe"
        return $false
    }
    Start-Process -FilePath $exe -WorkingDirectory $PSScriptRoot
    Start-Sleep -Milliseconds 1500
    return (Test-Running)
}

if ($InstallTask) {
    # 注册登录自启（比 Startup 文件夹更稳，可带延迟）
    $action  = New-ScheduledTaskAction -Execute 'powershell.exe' `
        -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    $trigger = New-ScheduledTaskTrigger -AtLogOn
    $trigger.Delay = 'PT10S'
    Register-ScheduledTask -TaskName 'Cavalry_ShortCut 看护' -Action $action -Trigger $trigger -Force | Out-Null
    Write-Host '[完成] 已注册登录自启任务：Cavalry_ShortCut 看护'
    exit 0
}

if ($Watch) {
    Write-Host "[看护中] 每 ${IntervalSeconds}s 检查一次，Ctrl+C 结束"
    while ($true) {
        if (-not (Test-Running)) {
            Write-Host ("[{0}] 进程不在，正在启动…" -f (Get-Date -Format 'HH:mm:ss'))
            if (Start-ShortCut) { Write-Host '        启动成功' } else { Write-Host '        启动失败' }
        }
        Start-Sleep -Seconds $IntervalSeconds
    }
}

# 默认：单次检查
if (Test-Running) {
    Write-Host '[信息] Cavalry_ShortCut 已在运行'
    exit 0
}
Write-Host '[信息] 未检测到进程，正在启动…'
if (Start-ShortCut) {
    Write-Host '[成功] 已启动 Cavalry_ShortCut'
    exit 0
} else {
    Write-Host '[失败] 启动后仍未检测到进程'
    exit 2
}
