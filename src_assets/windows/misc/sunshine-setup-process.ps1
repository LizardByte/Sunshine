# Write redirected process output through the setup script's logger.
function Write-RedirectedProcessOutput {
    <#
    .SYNOPSIS
    Writes non-empty lines from a redirected process stream to the setup log.

    .PARAMETER FilePath
    The file containing redirected process output.

    .PARAMETER Level
    The log level used for each output line.

    .PARAMETER Color
    An optional console color used for each output line.
    #>
    param(
        [string]$FilePath,
        [ValidateSet(
                "Information",
                "Warning"
        )]
        [string]$Level,
        [string]$Color = $null
    )

    if (-not (Test-Path $FilePath)) {
        return
    }

    $content = Get-Content $FilePath -Raw -ErrorAction SilentlyContinue
    if (-not $content) {
        return
    }

    $logParameters = @{
        Level = $Level
    }
    if ($Color) {
        $logParameters.Color = $Color
    }

    $content -split "`r?`n" | ForEach-Object {
        if ($_.Trim()) {
            Write-LogMessage -Message "  $_" @logParameters
        }
    }
}

# Start a process with standard output and standard error redirected to files.
function Invoke-RedirectedProcess {
    <#
    .SYNOPSIS
    Starts a process and waits for it while redirecting both output streams.

    .PARAMETER ExecutablePath
    The executable or script to run.

    .PARAMETER Arguments
    Optional arguments passed to the executable.

    .PARAMETER StandardOutputPath
    The file that receives standard output.

    .PARAMETER StandardErrorPath
    The file that receives standard error.
    #>
    param(
        [string]$ExecutablePath,
        [string]$Arguments = "",
        [string]$StandardOutputPath,
        [string]$StandardErrorPath
    )

    $startProcessParameters = @{
        FilePath = $ExecutablePath
        Wait = $true
        PassThru = $true
        NoNewWindow = $true
        RedirectStandardOutput = $StandardOutputPath
        RedirectStandardError = $StandardErrorPath
    }
    if ($Arguments) {
        $startProcessParameters.ArgumentList = $Arguments
    }

    return Start-Process @startProcessParameters
}

# Execute an executable if it exists and route its output through the setup logger.
function Invoke-ExecutableIfExist {
    <#
    .SYNOPSIS
    Runs an executable when present and reports its output and exit status.

    .PARAMETER ExecutablePath
    The executable or script to run.

    .PARAMETER Arguments
    Optional arguments passed to the executable.

    .PARAMETER Description
    An optional description logged before execution.

    .PARAMETER Emoji
    The icon prefixed to the execution description.

    .PARAMETER ExecutableName
    The friendly executable type used in failure messages.

    .PARAMETER MissingTarget
    The target name used when the executable does not exist.

    .PARAMETER FailureTarget
    An optional path appended to a non-zero exit message.
    #>
    param(
        [string]$ExecutablePath,
        [string]$Arguments = "",
        [string]$Description = "",
        [string]$Emoji = "🔧",
        [string]$ExecutableName,
        [string]$MissingTarget,
        [string]$FailureTarget = ""
    )

    if ($Description) {
        Write-LogMessage -Message "$Emoji $Description" -Level "Step"
    }

    if (-not (Test-Path $ExecutablePath)) {
        Write-LogMessage `
            -Message "  ⓘ Skipped ($MissingTarget not found)" `
            -Level "Information" `
            -Color "DarkGray"
        return 0
    }

    Write-LogMessage -Message "Executing: $ExecutablePath $Arguments" -Level "Information"

    $stdoutFile = [System.IO.Path]::GetTempFileName()
    $stderrFile = [System.IO.Path]::GetTempFileName()

    try {
        $process = Invoke-RedirectedProcess `
            -ExecutablePath $ExecutablePath `
            -Arguments $Arguments `
            -StandardOutputPath $stdoutFile `
            -StandardErrorPath $stderrFile

        Write-RedirectedProcessOutput `
            -FilePath $stdoutFile `
            -Level "Information" `
            -Color "DarkGray"
        Write-RedirectedProcessOutput -FilePath $stderrFile -Level "Warning"

        if ($process.ExitCode -ne 0) {
            $failureMessage = "  ⚠ $ExecutableName exited with code $($process.ExitCode)"
            if ($FailureTarget) {
                $failureMessage += ": $FailureTarget"
            }
            Write-LogMessage -Message $failureMessage -Level "Warning"
            return $process.ExitCode
        }

        Write-LogMessage -Message "  ✓ Done" -Level "Success"
        return 0
    } finally {
        Remove-Item `
            -LiteralPath $stdoutFile, $stderrFile `
            -Force `
            -ErrorAction SilentlyContinue
    }
}

# Execute a batch script if it exists.
function Invoke-ScriptIfExist {
    <#
    .SYNOPSIS
    Runs an installer batch script when it exists.

    .PARAMETER ScriptPath
    The batch script to run.

    .PARAMETER Arguments
    Optional arguments passed to the script.

    .PARAMETER Description
    An optional description logged before execution.

    .PARAMETER Emoji
    The icon prefixed to the execution description.
    #>
    param(
        [string]$ScriptPath,
        [string]$Arguments = "",
        [string]$Description = "",
        [string]$Emoji = "🔧"
    )

    return Invoke-ExecutableIfExist `
        -ExecutablePath $ScriptPath `
        -Arguments $Arguments `
        -Description $Description `
        -Emoji $Emoji `
        -ExecutableName "Script" `
        -MissingTarget "script" `
        -FailureTarget $ScriptPath
}

# Execute sunshine.exe with arguments if it exists.
function Invoke-SunshineIfExist {
    <#
    .SYNOPSIS
    Runs the packaged Sunshine executable when it exists.

    .PARAMETER Arguments
    Arguments passed to Sunshine.

    .PARAMETER Description
    An optional description logged before execution.

    .PARAMETER Emoji
    The icon prefixed to the execution description.
    #>
    param(
        [string]$Arguments,
        [string]$Description = "",
        [string]$Emoji = "🔧"
    )

    $SunshinePath = Join-Path $RootDir "sunshine.exe"
    return Invoke-ExecutableIfExist `
        -ExecutablePath $SunshinePath `
        -Arguments $Arguments `
        -Description $Description `
        -Emoji $Emoji `
        -ExecutableName "Sunshine" `
        -MissingTarget "executable"
}
