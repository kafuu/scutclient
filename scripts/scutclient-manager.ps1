$ErrorActionPreference = "Stop"

$Action = "help"
$Iface = ""
$Username = ""
$Password = ""
$Exe = ""
$TaskName = "scutclient"
$DebugMode = $false
$PauseOnExit = $false

function Read-OptionValue {
    param([string[]]$Values, [ref]$Index, [string]$OptionName)

    if ($Index.Value + 1 -ge $Values.Count) {
        throw "缺少 $OptionName 的参数值。"
    }
    $Index.Value++
    return $Values[$Index.Value]
}

if ($args.Count -gt 0) {
    $Action = $args[0].ToLowerInvariant()
}

$validActions = @("list", "install", "run", "stop", "status", "uninstall", "remove", "help", "--help", "-h")
if ($validActions -notcontains $Action) {
    throw "未知操作：$Action"
}
if ($Action -eq "--help" -or $Action -eq "-h") {
    $Action = "help"
}

for ($i = 1; $i -lt $args.Count; $i++) {
    switch -Regex ($args[$i]) {
        '^--?iface$' {
            $ref = [ref]$i
            $Iface = Read-OptionValue -Values $args -Index $ref -OptionName $args[$i]
            $i = $ref.Value
            continue
        }
        '^--?username$' {
            $ref = [ref]$i
            $Username = Read-OptionValue -Values $args -Index $ref -OptionName $args[$i]
            $i = $ref.Value
            continue
        }
        '^--?password$' {
            $ref = [ref]$i
            $Password = Read-OptionValue -Values $args -Index $ref -OptionName $args[$i]
            $i = $ref.Value
            continue
        }
        '^--?exe$' {
            $ref = [ref]$i
            $Exe = Read-OptionValue -Values $args -Index $ref -OptionName $args[$i]
            $i = $ref.Value
            continue
        }
        '^--?task-name$' {
            $ref = [ref]$i
            $TaskName = Read-OptionValue -Values $args -Index $ref -OptionName $args[$i]
            $i = $ref.Value
            continue
        }
        '^--?(debug|debug-mode)$' {
            $DebugMode = $true
            continue
        }
        '^--?pause-on-exit$' {
            $PauseOnExit = $true
            continue
        }
        default {
            throw "未知选项：$($args[$i])"
        }
    }
}

function Get-ProjectRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Resolve-ScutclientExe {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        $resolved = Resolve-Path -LiteralPath $ExplicitPath -ErrorAction SilentlyContinue
        if ($resolved) {
            return $resolved.Path
        }
        throw "未找到 scutclient.exe：$ExplicitPath"
    }

    $root = Get-ProjectRoot
    $candidates = @(
        (Join-Path $root "scutclient.exe"),
        (Join-Path $root "build\Release\scutclient.exe"),
        (Join-Path $root "build-vs2022-x64\Release\scutclient.exe"),
        (Join-Path $root "build-nmake-x64\scutclient.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "未找到 scutclient.exe。请将它放在 README.md 旁边、先完成构建，或通过 --exe <路径> 指定。"
}

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Require-Administrator {
    if (-not (Test-Administrator)) {
        throw "此操作必须在管理员权限的命令提示符或 PowerShell 中运行。"
    }
}

function Wait-ExitKey {
    Write-Host ""
    Write-Host "按任意键关闭窗口..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
}

function Convert-SecureStringToPlainText {
    param([Security.SecureString]$SecureString)

    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureString)
    try {
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
    } finally {
        if ($bstr -ne [IntPtr]::Zero) {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
        }
    }
}

function Get-Adapters {
    param([string]$ExePath)

    $output = & $ExePath --list-ifaces 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "使用 $ExePath 列出网卡失败。"
    }

    $items = @()
    $current = $null

    foreach ($line in $output) {
        if ($line -match '^\[(\d+)\]\s*$') {
            if ($current -and $current.Iface) {
                $items += [pscustomobject]$current
            }
            $current = [ordered]@{
                Iface = ""
                Friendly = ""
                Description = ""
                IP = ""
            }
            continue
        }

        if (-not $current) {
            continue
        }

        if ($line -match '^\s*iface:\s*(.+?)\s*$') {
            $current.Iface = $Matches[1]
        } elseif ($line -match '^\s*friendly:\s*(.+?)\s*$') {
            $current.Friendly = $Matches[1]
        } elseif ($line -match '^\s*description:\s*(.+?)\s*$') {
            $current.Description = $Matches[1]
        } elseif ($line -match '^\s*ip:\s*(.+?)\s*$') {
            $current.IP = $Matches[1]
        }
    }

    if ($current -and $current.Iface) {
        $items += [pscustomobject]$current
    }

    return $items
}

function Show-Adapters {
    param([object[]]$Adapters)

    if (-not $Adapters -or $Adapters.Count -eq 0) {
        throw "未找到可用网卡。"
    }

    Write-Host ""
    Write-Host "可用网卡："
    for ($i = 0; $i -lt $Adapters.Count; $i++) {
        $adapter = $Adapters[$i]
        Write-Host ("[{0}] {1}" -f ($i + 1), $adapter.Friendly)
        Write-Host ("    网卡标识：{0}" -f $adapter.Iface)
        Write-Host ("    IP 地址： {0}" -f $adapter.IP)
        if ($adapter.Description -and $adapter.Description -ne "(none)") {
            Write-Host ("    描述：    {0}" -f $adapter.Description)
        }
        Write-Host ""
    }
}

function Select-Adapter {
    param([string]$ExePath)

    $adapters = Get-Adapters -ExePath $ExePath
    Show-Adapters -Adapters $adapters

    while ($true) {
        $choice = Read-Host "请选择网卡序号"
        if ($choice -match '^\d+$') {
            $index = [int]$choice
        } else {
            $index = 0
        }
        if ($index -ge 1 -and $index -le $adapters.Count) {
            return $adapters[$index - 1].Iface
        }
        Write-Host "选择无效，请输入 1 到 $($adapters.Count) 之间的数字。"
    }
}

function Quote-Argument {
    param([string]$Value)

    return '"' + ($Value -replace '"', '\"') + '"'
}

function Install-Task {
    Require-Administrator

    $exePath = Resolve-ScutclientExe -ExplicitPath $Exe

    if (-not $Iface) {
        $Iface = Select-Adapter -ExePath $exePath
    }
    if (-not $Username) {
        $Username = Read-Host "账号"
    }
    if (-not $Password) {
        $securePassword = Read-Host "密码" -AsSecureString
        $Password = Convert-SecureStringToPlainText -SecureString $securePassword
    }

    if (-not $Iface) {
        throw "必须指定网卡。"
    }
    if (-not $Username) {
        throw "必须输入账号。"
    }
    if (-not $Password) {
        throw "必须输入密码。"
    }

    $arguments = @(
        "--iface", (Quote-Argument $Iface),
        "--username", (Quote-Argument $Username),
        "--password", (Quote-Argument $Password)
    )
    if ($DebugMode) {
        $arguments += "--debug"
    }

    $taskCommand = (Quote-Argument $exePath) + " " + ($arguments -join " ")

    & schtasks /Create /TN $TaskName /SC ONLOGON /RU SYSTEM /RL HIGHEST /TR $taskCommand /F | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "使用 schtasks.exe 创建计划任务失败。"
    }

    Write-Host ""
    Write-Host "已安装计划任务：$TaskName"
    Write-Host "可执行文件：    $exePath"
    Write-Host "网卡：          $Iface"
    Write-Host "账号：          $Username"
    Write-Host ""
    Write-Host "该任务会在用户登录时以 SYSTEM 身份运行，不显示控制台窗口。"
    Write-Host ""
    Write-Host "正在立即启动计划任务..."
    schtasks /Run /TN $TaskName | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "计划任务已安装，但立即启动失败。可稍后执行 scripts\scutclient-manager.bat run 手动启动。"
    }
    Write-Host "计划任务已启动。"
}

function Show-Status {
    schtasks /Query /TN $TaskName /V /FO LIST
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Run-Task {
    Require-Administrator
    schtasks /Run /TN $TaskName
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Stop-Client {
    Require-Administrator

    Write-Host "正在停止计划任务实例（如果存在）..."
    schtasks /End /TN $TaskName | Out-Host

    Write-Host "正在停止 scutclient.exe 进程（如果存在）..."
    taskkill /F /IM scutclient.exe | Out-Host

    Write-Host "停止命令已执行完成。"
}

function Uninstall-Task {
    Require-Administrator
    schtasks /Delete /TN $TaskName /F
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Show-Help {
    Write-Host "用法："
    Write-Host "  scutclient-manager.bat install [--exe 路径] [--iface 网卡] [--username 账号] [--password 密码] [--task-name 名称] [--debug-mode]"
    Write-Host "  scutclient-manager.bat list [--exe 路径]"
    Write-Host "  scutclient-manager.bat run [--task-name 名称]"
    Write-Host "  scutclient-manager.bat stop [--task-name 名称]"
    Write-Host "  scutclient-manager.bat status [--task-name 名称]"
    Write-Host "  scutclient-manager.bat uninstall [--task-name 名称]"
    Write-Host ""
    Write-Host "常用交互式安装："
    Write-Host "  scutclient-manager.bat install"
}

try {
    switch ($Action) {
        "list" {
            $exePath = Resolve-ScutclientExe -ExplicitPath $Exe
            $adapters = Get-Adapters -ExePath $exePath
            Show-Adapters -Adapters $adapters
        }
        "install" { Install-Task }
        "run" { Run-Task }
        "stop" { Stop-Client }
        "status" { Show-Status }
        "uninstall" { Uninstall-Task }
        "remove" { Uninstall-Task }
        "help" { Show-Help }
    }
    if ($PauseOnExit) {
        Wait-ExitKey
    }
} catch {
    [Console]::Error.WriteLine("错误：" + $_.Exception.Message)
    if ($_.InvocationInfo) {
        [Console]::Error.WriteLine("位置：" + $_.InvocationInfo.PositionMessage)
    }
    if ($PauseOnExit) {
        Wait-ExitKey
    }
    exit 1
}
