BeforeAll {
    $sourcePath = Join-Path $PSScriptRoot "..\..\src_assets\windows\misc\path\update-path.bat"
    $source = Get-Content -LiteralPath $sourcePath -Raw
    $systemKey = 'HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
    $source.Contains($systemKey) | Should -BeTrue

    # Run the packaged batch script against an isolated test key, never the system PATH.
    $script:registrySubKey = "Software\Sunshine.Tests\$([guid]::NewGuid().ToString('N'))"
    $script:registryKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($script:registrySubKey)
    $script:sunshineRoot = Join-Path $TestDrive "Sunshine install"
    $scriptDirectory = Join-Path $script:sunshineRoot "scripts"
    New-Item -ItemType Directory -Path $scriptDirectory -Force | Out-Null
    $script:batchPath = Join-Path $scriptDirectory "update-path.bat"
    Set-Content `
        -LiteralPath $script:batchPath `
        -Value $source.Replace($systemKey, "HKEY_CURRENT_USER\$script:registrySubKey") `
        -Encoding Ascii

    function Invoke-PathUpdate {
        <#
        .SYNOPSIS
        Runs the batch script with its registry access redirected to the test key.

        .PARAMETER Action
        The add or remove operation to run.
        #>
        param([string]$Action)

        $output = & $env:ComSpec /d /c "`"$script:batchPath`" $Action" 2>&1
        $LASTEXITCODE | Should -Be 0 -Because ($output -join "`n")
    }
}

AfterAll {
    if ($script:registryKey) {
        $script:registryKey.Dispose()
        [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($script:registrySubKey)
    }
}

Describe "update-path.bat" {
    It "preserves unrelated entries ending in <TrailingBackslashes> backslashes across install and uninstall" -TestCases @(
        @{ TrailingBackslashes = 0 }
        @{ TrailingBackslashes = 1 }
        @{ TrailingBackslashes = 2 }
        @{ TrailingBackslashes = 3 }
    ) {
        param($TrailingBackslashes)

        $original = '%SystemRoot%;C:\Program Files\WSL' + ('\' * $TrailingBackslashes)
        $script:registryKey.SetValue('Path', $original, [Microsoft.Win32.RegistryValueKind]::ExpandString)

        Invoke-PathUpdate -Action "add"

        $script:registryKey.GetValue('Path', $null, 'DoNotExpandEnvironmentNames') |
            Should -BeExactly "$original;$script:sunshineRoot;$script:sunshineRoot\tools"
        $script:registryKey.GetValueKind('Path') | Should -Be 'ExpandString'

        Invoke-PathUpdate -Action "remove"

        $script:registryKey.GetValue('Path', $null, 'DoNotExpandEnvironmentNames') |
            Should -BeExactly $original
        $script:registryKey.GetValueKind('Path') | Should -Be 'ExpandString'

        Invoke-PathUpdate -Action "add"

        $script:registryKey.GetValue('Path', $null, 'DoNotExpandEnvironmentNames') |
            Should -BeExactly "$original;$script:sunshineRoot;$script:sunshineRoot\tools"
    }

    It "preserves a trailing backslash when Sunshine entries occur in the middle of PATH" {
        $original = "C:\Windows;$script:sunshineRoot;C:\Other;$script:sunshineRoot\tools;C:\Last\"
        $script:registryKey.SetValue('Path', $original, [Microsoft.Win32.RegistryValueKind]::ExpandString)

        Invoke-PathUpdate -Action "remove"

        $script:registryKey.GetValue('Path', $null, 'DoNotExpandEnvironmentNames') |
            Should -BeExactly 'C:\Windows;C:\Other;C:\Last\'
    }

    It "can remove Sunshine when it is the only PATH entry" {
        $script:registryKey.SetValue('Path', $script:sunshineRoot, [Microsoft.Win32.RegistryValueKind]::ExpandString)

        Invoke-PathUpdate -Action "remove"

        $script:registryKey.GetValue('Path', $null, 'DoNotExpandEnvironmentNames') | Should -BeExactly ''
    }

    It "does not rewrite the value when both Sunshine directories are already present" {
        $original = "$script:sunshineRoot;$script:sunshineRoot\tools;C:\Last\"
        $script:registryKey.SetValue('Path', $original, [Microsoft.Win32.RegistryValueKind]::String)

        Invoke-PathUpdate -Action "add"

        $script:registryKey.GetValue('Path') | Should -BeExactly $original
        $script:registryKey.GetValueKind('Path') | Should -Be 'String'
    }
}
