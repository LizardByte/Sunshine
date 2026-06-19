# Sunshine Setup Script
# This script orchestrates the installation and uninstallation of Sunshine
# Usage: sunshine-setup.ps1 -Action [install|uninstall] [-Silent]

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet(
            "install",
            "uninstall"
    )]
    [string]$Action,

    [Parameter(Mandatory=$false)]
    [switch]$Silent
)

# Constants
$DocsUrl = "https://docs.lizardbyte.dev/projects/sunshine"

# Set preference variables for output streams
$InformationPreference = 'Continue'

# Function to write output to both console (with color/stream) and log file (without color)
function Write-LogMessage {
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '',
        Justification='Write-Host is required for colored output')]
    param(
        [Parameter(Mandatory=$true)]
        [AllowEmptyString()]
        [string]$Message,

        [Parameter(Mandatory=$false)]
        [ValidateSet(
                'Debug',
                'Error',
                'Information',
                'Step',
                'Success',
                'Verbose',
                'Warning'
        )]
        [string]$Level = 'Information',

        [Parameter(Mandatory=$false)]
        [ValidateSet(
                'Black',
                'Blue',
                'Cyan',
                'DarkGray',
                'Gray',
                'Green',
                'Magenta',
                'Red',
                'White',
                'Yellow'
        )]
        [string]$Color = $null,

        [Parameter(Mandatory=$false)]
        [switch]$NoTimestamp,

        [Parameter(Mandatory=$false)]
        [switch]$NoLogFile
    )

    # Map levels to colors and output streams
    $levelConfig = @{
        'Debug' = @{ DefaultColor = 'DarkGray'; Stream = 'Debug'; Emoji = ''; LogLevel = 'DEBUG' }
        'Error' = @{ DefaultColor = 'Red'; Stream = 'Error'; Emoji = '✗'; LogLevel = 'ERROR' }
        'Information' = @{ DefaultColor = $null; Stream = 'Host'; Emoji = ''; LogLevel = 'INFO' }
        'Step' = @{ DefaultColor = 'Cyan'; Stream = 'Host'; Emoji = '==>'; LogLevel = 'INFO' }
        'Success' = @{ DefaultColor = 'Green'; Stream = 'Host'; Emoji = '✓'; LogLevel = 'INFO' }
        'Verbose' = @{ DefaultColor = 'DarkGray'; Stream = 'Verbose'; Emoji = ''; LogLevel = 'VERBOSE' }
        'Warning' = @{ DefaultColor = 'Yellow'; Stream = 'Warning'; Emoji = '⚠'; LogLevel = 'WARN' }
    }

    $config = $levelConfig[$Level]

    # Use custom color if specified, otherwise use default color for the level
    $displayColor = if ($Color) { $Color } else { $config.DefaultColor }

    # Write to appropriate output stream with color
    switch ($config.Stream) {
        'Debug' {
            Write-Debug $Message
        }
        'Error' {
            Write-Error $Message
        }
        'Host' {
            if ($null -ne $displayColor) {
                Write-Host "$($config.Emoji) $Message" -ForegroundColor $displayColor
            } else {
                Write-Host "$($config.Emoji) $Message"
            }
        }
        'Information' {
            Write-Information $Message
        }
        'Verbose' {
            Write-Verbose $Message
        }
        'Warning' {
            Write-Warning $Message
        }
        default {
            Write-Information $Message
        }
    }

    # Write to log file without color codes (only if LogPath exists and not disabled)
    if ($script:LogPath -and -not $NoLogFile) {
        try {
            # Format log entry with timestamp and level
            if ($NoTimestamp) {
                $logEntry = $Message
            } else {
                $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
                $logEntry = "[$timestamp] [$($config.LogLevel)] $Message"
            }

            $logEntry | Out-File `
                -FilePath $script:LogPath `
                -Append `
                -Encoding UTF8
        } catch {
            # Avoid infinite recursion - use Write-Verbose directly
            Write-Verbose "Could not write to log file: $($_.Exception.Message)"
        }
    }
}

# Function to print a separator bar
function Write-Bar {
    param(
        [string]$Level = 'Information',
        [int]$Length = 63,
        [string]$Color = $null,
        [switch]$NoTimestamp
    )
    $bar = "=" * $Length
    if ($Color) {
        Write-LogMessage -Message $bar -Level $Level -Color $Color -NoTimestamp:$NoTimestamp
    } else {
        Write-LogMessage -Message $bar -Level $Level -NoTimestamp:$NoTimestamp
    }
}

# Function to print text framed by bars
function Write-FramedText {
    param(
        [string]$Message,
        [string]$Level = 'Information',
        [int]$BarLength = 63,
        [string]$Color = $null,
        [switch]$NoTimestamp,
        [switch]$NoCenter
    )

    # Center the message if NoCenter is not specified
    $displayMessage = $Message
    if (-not $NoCenter) {
        $messageLength = $Message.Trim().Length

        if ($messageLength -lt $BarLength) {
            $totalPadding = $BarLength - $messageLength
            $leftPadding = [Math]::Floor($totalPadding / 2)
            $displayMessage = (' ' * $leftPadding) + $Message.Trim()
        } else {
            $displayMessage = $Message.Trim()
        }
    }

    if ($Color) {
        Write-Bar -Level $Level -Length $BarLength -Color $Color -NoTimestamp:$NoTimestamp
        Write-LogMessage -Message $displayMessage -Level $Level -Color $Color -NoTimestamp:$NoTimestamp
        Write-Bar -Level $Level -Length $BarLength -Color $Color -NoTimestamp:$NoTimestamp
    } else {
        Write-Bar -Level $Level -Length $BarLength -NoTimestamp:$NoTimestamp
        Write-LogMessage -Message $displayMessage -Level $Level -NoTimestamp:$NoTimestamp
        Write-Bar -Level $Level -Length $BarLength -NoTimestamp:$NoTimestamp
    }
}

# Function to write to log file (helper function)
function Write-LogFile {
    param(
        [string[]]$Lines
    )
    if ($script:LogPath) {
        try {
            foreach ($line in $Lines) {
                $line | Out-File `
                    -FilePath $script:LogPath `
                    -Append `
                    -Encoding UTF8
            }
        } catch {
            Write-Warning "Failed to write to log file: $($_.Exception.Message)"
        }
    }
}

# If Action is not provided, prompt the user
if (-not $Action) {
    Write-Information ""
    Write-FramedText -Message "🔅 Sunshine Setup Script" -Level "Information" -Color "Cyan"
    Write-Information ""
    Write-LogMessage -Message "Please select an action:" -Level "Information" -Color "Yellow"
    Write-LogMessage -Message "  1. Install Sunshine" -Level "Information" -Color "Green"
    Write-LogMessage -Message "  2. Uninstall Sunshine" -Level "Information" -Color "Red"
    Write-Information ""

    $validChoice = $false
    while (-not $validChoice) {
        $choice = Read-Host "Enter your choice (1 or 2)"

        switch ($choice) {
            "1" {
                $Action = "install"
                $validChoice = $true
            }
            "2" {
                $Action = "uninstall"
                $validChoice = $true
            }
            default {
                Write-Warning "Invalid choice. Please select 1 or 2."
                Write-Information ""
            }
        }
    }
    Write-Information ""
}

# Check if running as administrator, if not, relaunch with elevation
$currentPrincipal = New-Object `
        Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Warning "This script requires administrator privileges. Relaunching with elevation..."

    # Build the argument list for the elevated process
    $arguments = "-ExecutionPolicy Bypass -File `"$($MyInvocation.MyCommand.Path)`" -Action $Action"
    if ($Silent) {
        $arguments += " -Silent"
    }

    try {
        # Relaunch the script with elevation
        Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait
        exit $LASTEXITCODE
    } catch {
        Write-Error "Failed to elevate privileges: $($_.Exception.Message)"
        exit 1
    }
}

# Get the script directory and root directory
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir

# Set up transcript logging
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logDir = Join-Path $env:TEMP "Sunshine\logs\$Action"
$LogPath = Join-Path $logDir "${timestamp}.log"

# Ensure the log directory exists
if (-not (Test-Path $logDir)) {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
}

# Store LogPath in script scope for logging functions
$script:LogPath = $LogPath

# Function to execute a batch script if it exists
function Invoke-ScriptIfExist {
    param(
        [string]$ScriptPath,
        [string]$Arguments = "",
        [string]$Description = "",
        [string]$Emoji = "🔧"
    )

    if ($Description) {
        Write-LogMessage -Message "$Emoji $Description" -Level "Step"
    }

    if (Test-Path $ScriptPath) {
        Write-LogMessage -Message "Executing: $ScriptPath $Arguments" -Level "Information"

        # Capture output to suppress it from console but log it
        $stdoutFile = [System.IO.Path]::GetTempFileName()
        $stderrFile = [System.IO.Path]::GetTempFileName()

        try {
            if ($Arguments -ne "") {
                $process = Start-Process `
                    -FilePath $ScriptPath `
                    -ArgumentList $Arguments `
                    -Wait `
                    -PassThru `
                    -NoNewWindow `
                    -RedirectStandardOutput $stdoutFile `
                    -RedirectStandardError $stderrFile
            } else {
                $process = Start-Process `
                    -FilePath $ScriptPath `
                    -Wait `
                    -PassThru `
                    -NoNewWindow `
                    -RedirectStandardOutput $stdoutFile `
                    -RedirectStandardError $stderrFile
            }

            # Log and display the output
            if (Test-Path $stdoutFile) {
                $output = Get-Content $stdoutFile -Raw -ErrorAction SilentlyContinue
                if ($output) {
                    # Display output with indentation
                    $output -split "`r?`n" | ForEach-Object {
                        if ($_.Trim()) {
                            Write-LogMessage -Message "  $_" -Level "Information" -Color "DarkGray"
                        }
                    }
                }
            }
            if (Test-Path $stderrFile) {
                $errors = Get-Content $stderrFile -Raw -ErrorAction SilentlyContinue
                if ($errors) {
                    # Display errors with indentation
                    $errors -split "`r?`n" | ForEach-Object {
                        if ($_.Trim()) {
                            Write-LogMessage -Message "  $_" -Level "Warning"
                        }
                    }
                }
            }

            if ($process.ExitCode -ne 0) {
                Write-LogMessage -Message "  ⚠ Script exited with code $($process.ExitCode): $ScriptPath" -Level "Warning"
                return $process.ExitCode
            } else {
                Write-LogMessage -Message "  ✓ Done" -Level "Success"
                return 0
            }
        } finally {
            # Clean up temp files
            if (Test-Path $stdoutFile) {
                Remove-Item $stdoutFile -Force -ErrorAction SilentlyContinue
            }
            if (Test-Path $stderrFile) {
                Remove-Item $stderrFile -Force -ErrorAction SilentlyContinue
            }
        }
    } else {
        Write-LogMessage -Message "  ⓘ Skipped (script not found)" -Level "Information" -Color "DarkGray"
        return 0
    }
}

# Function to execute sunshine.exe with arguments if it exists
function Invoke-SunshineIfExist {
    param(
        [string]$Arguments,
        [string]$Description = "",
        [string]$Emoji = "🔧"
    )

    if ($Description) {
        Write-LogMessage -Message "$Emoji $Description" -Level "Step"
    }

    $SunshinePath = Join-Path $RootDir "sunshine.exe"

    if (Test-Path $SunshinePath) {
        Write-LogMessage -Message "Executing: $SunshinePath $Arguments" -Level "Information"

        # Capture output to suppress it from console but log it
        $stdoutFile = [System.IO.Path]::GetTempFileName()
        $stderrFile = [System.IO.Path]::GetTempFileName()

        try {
            $process = Start-Process `
                -FilePath $SunshinePath `
                -ArgumentList $Arguments `
                -Wait `
                -PassThru `
                -NoNewWindow `
                -RedirectStandardOutput $stdoutFile `
                -RedirectStandardError $stderrFile

            # Log and display the output
            if (Test-Path $stdoutFile) {
                $output = Get-Content $stdoutFile -Raw -ErrorAction SilentlyContinue
                if ($output) {
                    # Display output with indentation
                    $output -split "`r?`n" | ForEach-Object {
                        if ($_.Trim()) {
                            Write-LogMessage -Message "  $_" -Level "Information" -Color "DarkGray"
                        }
                    }
                }
            }
            if (Test-Path $stderrFile) {
                $errors = Get-Content $stderrFile -Raw -ErrorAction SilentlyContinue
                if ($errors) {
                    # Display errors with indentation
                    $errors -split "`r?`n" | ForEach-Object {
                        if ($_.Trim()) {
                            Write-LogMessage -Message "  $_" -Level "Warning"
                        }
                    }
                }
            }

            if ($process.ExitCode -ne 0) {
                Write-LogMessage -Message "  ⚠ Sunshine exited with code $($process.ExitCode)" -Level "Warning"
                return $process.ExitCode
            } else {
                Write-LogMessage -Message "  ✓ Done" -Level "Success"
                return 0
            }
        } finally {
            # Clean up temp files
            if (Test-Path $stdoutFile) {
                Remove-Item $stdoutFile -Force -ErrorAction SilentlyContinue
            }
            if (Test-Path $stderrFile) {
                Remove-Item $stderrFile -Force -ErrorAction SilentlyContinue
            }
        }
    } else {
        Write-LogMessage -Message "  ⓘ Skipped (executable not found)" -Level "Information" -Color "DarkGray"
        return 0
    }
}

# Main script logic
Write-Information ""

if ($Action -eq "install") {
    Write-FramedText `
        -Message "🔅 Sunshine Installation Script" `
        -Level "Information" `
        -Color "Yellow"
    Write-Information ""

    $totalSteps = 6
    $currentStep = 0

    # Reset permissions on the install directory
    $currentStep++
    Write-Progress `
        -Activity "Installing Sunshine" `
        -Status "Resetting permissions on installation directory" `
        -PercentComplete (($currentStep / $totalSteps) * 100)
    Write-LogMessage -Message "🔐 Resetting permissions on installation directory" -Level "Step"
    try {
        Write-LogMessage -Message "Executing: icacls.exe `"$RootDir`" /reset" -Level "Information"

        # Capture output to suppress it from console but log it
        $stdoutFile = [System.IO.Path]::GetTempFileName()
        $stderrFile = [System.IO.Path]::GetTempFileName()

        try {
            $icaclsProcess = Start-Process `
                -FilePath "icacls.exe" `
                -ArgumentList "`"$RootDir`" /reset" `
                -Wait `
                -PassThru `
                -NoNewWindow `
                -RedirectStandardOutput $stdoutFile `
                -RedirectStandardError $stderrFile

            # Log and display the output
            if (Test-Path $stdoutFile) {
                $output = Get-Content $stdoutFile -Raw -ErrorAction SilentlyContinue
                if ($output) {
                    # Display output with indentation
                    $output -split "`r?`n" | ForEach-Object {
                        if ($_.Trim()) {
                            Write-LogMessage -Message "  $_" -Level "Information" -Color "DarkGray"
                        }
                    }
                }
            }
            if (Test-Path $stderrFile) {
                $errors = Get-Content $stderrFile -Raw -ErrorAction SilentlyContinue
                if ($errors) {
                    # Display errors with indentation
                    $errors -split "`r?`n" | ForEach-Object {
                        if ($_.Trim()) {
                            Write-LogMessage -Message "  $_" -Level "Warning"
                        }
                    }
                }
            }

            if ($icaclsProcess.ExitCode -eq 0) {
                Write-LogMessage -Message "  ✓ Done" -Level "Success"
            } else {
                Write-LogMessage -Message "  ⚠ Exit code $($icaclsProcess.ExitCode)" -Level "Warning"
            }
        } finally {
            # Clean up temp files
            if (Test-Path $stdoutFile) {
                Remove-Item $stdoutFile -Force -ErrorAction SilentlyContinue
            }
            if (Test-Path $stderrFile) {
                Remove-Item $stderrFile -Force -ErrorAction SilentlyContinue
            }
        }
    } catch {
        Write-LogMessage -Message "  ⚠ Failed to reset permissions: $($_.Exception.Message)" -Level "Warning"
    }
    Write-Information ""

    # 1. Update PATH (add)
    $currentStep++
    Write-Progress `
        -Activity "Installing Sunshine" `
        -Status "Updating system PATH" `
        -PercentComplete (($currentStep / $totalSteps) * 100)
    $updatePathScript = Join-Path $RootDir "scripts\update-path.bat"
    Invoke-ScriptIfExist `
        -ScriptPath $updatePathScript `
        -Arguments "add" `
        -Description "Adding Sunshine directories to PATH" `
        -Emoji "📁"
    Write-Information ""

    # 2. Migrate configuration
    $currentStep++
    Write-Progress `
        -Activity "Installing Sunshine" `
        -Status "Migrating configuration" `
        -PercentComplete (($currentStep / $totalSteps) * 100)
    $migrateConfigScript = Join-Path $RootDir "scripts\migrate-config.bat"
    Invoke-ScriptIfExist `
        -ScriptPath $migrateConfigScript `
        -Description "Migrating configuration files" `
        -Emoji "⚙️"
    Write-Information ""

    # 3. Add firewall rules
    $currentStep++
    Write-Progress `
        -Activity "Installing Sunshine" `
        -Status "Configuring firewall" `
        -PercentComplete (($currentStep / $totalSteps) * 100)
    $addFirewallScript = Join-Path $RootDir "scripts\add-firewall-rule.bat"
    Invoke-ScriptIfExist `
        -ScriptPath $addFirewallScript `
        -Description "Adding firewall rules" `
        -Emoji "🛡️"
    Write-Information ""

    # 4. Install service
    $currentStep++
    Write-Progress `
        -Activity "Installing Sunshine" `
        -Status "Installing service" `
        -PercentComplete (($currentStep / $totalSteps) * 100)
    $installServiceScript = Join-Path $RootDir "scripts\install-service.bat"
    Invoke-ScriptIfExist `
        -ScriptPath $installServiceScript `
        -Description "Installing Windows Service" `
        -Emoji "⚡"
    Write-Information ""

    # 5. Configure autostart
    $currentStep++
    Write-Progress `
        -Activity "Installing Sunshine" `
        -Status "Configuring autostart" `
        -PercentComplete (($currentStep / $totalSteps) * 100)
    $autostartScript = Join-Path $RootDir "scripts\autostart-service.bat"
    Invoke-ScriptIfExist `
        -ScriptPath $autostartScript `
        -Description "Configuring autostart" `
        -Emoji "🚀"
    Write-Information ""

    Write-Progress -Activity "Installing Sunshine" -Completed
    Write-FramedText -Message "✓ Sunshine installation completed successfully!" -Level "Success"

    # Open documentation in browser (only if not running silently)
    if (-not $Silent) {
        Write-Information ""
        Write-LogMessage `
            -Message "📖 Opening documentation in your browser: $DocsUrl" `
            -Level "Step"
        try {
            Start-Process $DocsUrl
            Write-LogMessage -Message "  ✓ Done" -Level "Success"
        } catch {
            Write-LogMessage `
                -Message "  ⓘ Could not open browser automatically: $($_.Exception.Message)" `
                -Level "Warning"
        }
    }

} elseif ($Action -eq "uninstall") {
    Write-FramedText `
        -Message "🗑️  Sunshine Uninstallation Script" `
        -Level "Information" `
        -Color "Yellow"
    Write-Information ""

    $totalSteps = 4
    $currentStep = 0

    # 1. Delete firewall rules
    $currentStep++
    Write-Progress `
        -Activity "Uninstalling Sunshine" `
        -Status "Removing firewall rules" `
        -PercentComplete (($currentStep / $totalSteps) * 100)
    $deleteFirewallScript = Join-Path $RootDir "scripts\delete-firewall-rule.bat"
    Invoke-ScriptIfExist `
        -ScriptPath $deleteFirewallScript `
        -Description "Removing firewall rules" `
        -Emoji "🛡️"
    Write-Information ""

    # 2. Uninstall service
    $currentStep++
    Write-Progress `
        -Activity "Uninstalling Sunshine" `
        -Status "Uninstalling service" `
        -PercentComplete (($currentStep / $totalSteps) * 100)
    $uninstallServiceScript = Join-Path $RootDir "scripts\uninstall-service.bat"
    Invoke-ScriptIfExist `
        -ScriptPath $uninstallServiceScript `
        -Description "Removing Windows Service" `
        -Emoji "⚡"
    Write-Information ""

    # 3. Restore NVIDIA preferences
    $currentStep++
    Write-Progress `
        -Activity "Uninstalling Sunshine" `
        -Status "Restoring NVIDIA settings" `
        -PercentComplete (($currentStep / $totalSteps) * 100)
    Invoke-SunshineIfExist `
        -Arguments "--restore-nvprefs-undo" `
        -Description "Restoring NVIDIA preferences" `
        -Emoji "🎮"
    Write-Information ""

    # 4. Update PATH (remove)
    $currentStep++
    Write-Progress `
        -Activity "Uninstalling Sunshine" `
        -Status "Cleaning up system PATH" `
        -PercentComplete (($currentStep / $totalSteps) * 100)
    $updatePathScript = Join-Path $RootDir "scripts\update-path.bat"
    Invoke-ScriptIfExist `
        -ScriptPath $updatePathScript `
        -Arguments "remove" `
        -Description "Removing from PATH" `
        -Emoji "📁"
    Write-Information ""

    Write-Progress -Activity "Uninstalling Sunshine" -Completed
    Write-FramedText `
        -Message "✓ Sunshine uninstallation completed successfully!" `
        -Level "Success"
}

Write-Information ""
exit 0
