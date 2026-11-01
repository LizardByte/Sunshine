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

# Normalize a PATH entry for comparison without changing how it is stored.
function ConvertTo-NormalizedPathEntry {
    <#
    .SYNOPSIS
    Normalizes a PATH entry for case-insensitive comparison.

    .PARAMETER Entry
    The PATH entry to normalize.

    .OUTPUTS
    The trimmed entry without a trailing directory separator, except for drive roots.
    #>
    param(
        [string]$Entry
    )

    $trimmed = $Entry.Trim()
    if ($trimmed -match '^[A-Za-z]:\\$') {
        return $trimmed
    }
    return $trimmed.TrimEnd('\')
}

# Normalize and validate a list of PATH entries.
function ConvertTo-NormalizedPathEntryList {
    <#
    .SYNOPSIS
    Normalizes PATH entries and rejects empty ones.

    .PARAMETER Entries
    Entries to normalize.

    .PARAMETER Action
    Action used in the error message, such as add or remove.

    .OUTPUTS
    An object whose Entries property contains the normalized entries.
    #>
    param(
        [string[]]$Entries,
        [string]$Action
    )

    $normalized = @()
    foreach ($entry in $Entries) {
        $value = ConvertTo-NormalizedPathEntry -Entry $entry
        if (-not $value) {
            throw "Cannot $Action an empty PATH entry."
        }
        $normalized += $value
    }

    return [PSCustomObject]@{
        Entries = $normalized
    }
}

# Split a PATH list into kept and removed entries.
function Split-PathEntryList {
    <#
    .SYNOPSIS
    Splits a PATH list into kept and removed entries.

    .PARAMETER Entries
    Current PATH entries.

    .PARAMETER Remove
    Normalized entries to remove.

    .OUTPUTS
    An object with the kept entries and the removed entries.
    #>
    param(
        [string[]]$Entries,
        [string[]]$Remove
    )

    $removeSet = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase
    )
    foreach ($entry in $Remove) {
        [void]$removeSet.Add($entry)
    }

    $kept = @()
    $removed = @()
    foreach ($entry in $Entries) {
        if ($removeSet.Contains((ConvertTo-NormalizedPathEntry -Entry $entry))) {
            $removed += $entry
        } else {
            $kept += $entry
        }
    }

    return [PSCustomObject]@{
        Entries = $kept
        Removed = $removed
    }
}

# Join new entries into a PATH list without duplicates.
function Join-PathEntryList {
    <#
    .SYNOPSIS
    Joins new entries into a PATH list, skipping duplicates.

    .PARAMETER Entries
    Current PATH entries.

    .PARAMETER Add
    Normalized entries to append.

    .OUTPUTS
    An object with the resulting entries and the added entries.
    #>
    param(
        [string[]]$Entries,
        [string[]]$Add
    )

    $existing = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase
    )
    foreach ($entry in $Entries) {
        [void]$existing.Add((ConvertTo-NormalizedPathEntry -Entry $entry))
    }

    $result = @($Entries)
    $added = @()
    foreach ($entry in $Add) {
        if ($existing.Add($entry)) {
            $result += $entry
            $added += $entry
        }
    }

    return [PSCustomObject]@{
        Entries = $result
        Added = $added
    }
}

# Notify the system that environment variables changed.
function Send-EnvironmentChange {
    <#
    .SYNOPSIS
    Broadcasts WM_SETTINGCHANGE so new processes pick up environment changes.
    #>

    if (-not ("Sunshine.NativeMethods" -as [type])) {
        Add-Type -Namespace Sunshine -Name NativeMethods -MemberDefinition @'
[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint Msg, UIntPtr wParam,
    string lParam, uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);
'@
    }

    $result = [UIntPtr]::Zero
    [void][Sunshine.NativeMethods]::SendMessageTimeout(
        [IntPtr]0xffff,
        0x001A,
        [UIntPtr]::Zero,
        "Environment",
        0x0002,
        5000,
        [ref]$result
    )
}

# Add or remove entries in an environment variable containing a semicolon-separated path list.
function Invoke-PathUpdate {
    <#
    .SYNOPSIS
    Adds or removes PATH entries without expanding or rewriting unrelated entries.

    .PARAMETER Add
    Entries to append. Entries that already exist are skipped.

    .PARAMETER Remove
    Entries to remove. Matching is case-insensitive and ignores a trailing directory separator.

    .PARAMETER Hive
    Registry hive containing the environment key. Defaults to the local machine hive.

    .PARAMETER SubKey
    Registry key containing the environment value.

    .PARAMETER ValueName
    Name of the environment value. Defaults to Path.

    .OUTPUTS
    An object describing whether the value changed and which entries were added or removed.
    #>
    param(
        [string[]]$Add = @(),
        [string[]]$Remove = @(),
        [Microsoft.Win32.RegistryKey]$Hive = [Microsoft.Win32.Registry]::LocalMachine,
        [string]$SubKey = "SYSTEM\CurrentControlSet\Control\Session Manager\Environment",
        [string]$ValueName = "Path"
    )

    $addEntries = ConvertTo-NormalizedPathEntryList -Entries $Add -Action "add"
    $removeEntries = ConvertTo-NormalizedPathEntryList -Entries $Remove -Action "remove"

    $key = $Hive.OpenSubKey($SubKey, $true)
    if (-not $key) {
        throw "Registry key not found: $SubKey"
    }

    try {
        $raw = [string]$key.GetValue(
            $ValueName,
            "",
            [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames
        )
        $entries = @()
        if ($raw) {
            $entries = @($raw -split ';')
        }

        $added = @()
        $removed = @()
        if ($removeEntries.Entries.Count -gt 0) {
            $removeResult = Split-PathEntryList -Entries $entries -Remove $removeEntries.Entries
            $entries = $removeResult.Entries
            $removed = $removeResult.Removed
        }

        if ($addEntries.Entries.Count -gt 0) {
            $addResult = Join-PathEntryList -Entries $entries -Add $addEntries.Entries
            $entries = $addResult.Entries
            $added = $addResult.Added
        }

        $changed = ($added.Count + $removed.Count) -gt 0
        if ($changed) {
            $key.SetValue(
                $ValueName,
                ($entries -join ';'),
                [Microsoft.Win32.RegistryValueKind]::ExpandString
            )
            Send-EnvironmentChange
        }
    } finally {
        $key.Dispose()
    }

    return [PSCustomObject]@{
        Changed = $changed
        Added = $added
        Removed = $removed
    }
}
