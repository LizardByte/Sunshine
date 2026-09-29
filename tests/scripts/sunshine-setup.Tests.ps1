BeforeAll {
    $sourcePath = Join-Path `
        $PSScriptRoot `
        "..\..\src_assets\windows\misc\sunshine-setup-process.ps1"

    function Write-LogMessage {
        param(
            [string]$Message,
            [string]$Level,
            [string]$Color
        )

        $null = $Message, $Level, $Color
    }

    . $sourcePath
}

Describe "Write-RedirectedProcessOutput" {
    BeforeEach {
        Mock Write-LogMessage {}
    }

    It "logs each non-empty redirected output line" {
        $outputPath = Join-Path $TestDrive "process-output.txt"
        Set-Content -LiteralPath $outputPath -Value @("first", "", "second")

        Write-RedirectedProcessOutput `
            -FilePath $outputPath `
            -Level "Information" `
            -Color "DarkGray"

        Should -Invoke -CommandName Write-LogMessage -Times 1 -Exactly -Scope It -ParameterFilter {
            $Message -eq "  first" -and
                $Level -eq "Information" -and
                $Color -eq "DarkGray"
        }
        Should -Invoke -CommandName Write-LogMessage -Times 1 -Exactly -Scope It -ParameterFilter {
            $Message -eq "  second" -and
                $Level -eq "Information" -and
                $Color -eq "DarkGray"
        }
    }

    It "ignores a missing redirected output file" {
        Write-RedirectedProcessOutput `
            -FilePath (Join-Path $TestDrive "missing.txt") `
            -Level "Warning"

        Should -Invoke -CommandName Write-LogMessage -Times 0 -Exactly -Scope It
    }

    It "ignores an empty redirected output file" {
        $outputPath = Join-Path $TestDrive "empty.txt"
        New-Item -ItemType File -Path $outputPath | Out-Null

        Write-RedirectedProcessOutput -FilePath $outputPath -Level "Warning"

        Should -Invoke -CommandName Write-LogMessage -Times 0 -Exactly -Scope It
    }

    It "logs redirected output without an optional color" {
        $outputPath = Join-Path $TestDrive "process-error.txt"
        Set-Content -LiteralPath $outputPath -Value "failure"

        Write-RedirectedProcessOutput -FilePath $outputPath -Level "Warning"

        Should -Invoke -CommandName Write-LogMessage -Times 1 -Exactly -Scope It -ParameterFilter {
            $Message -eq "  failure" -and
                $Level -eq "Warning" -and
                -not $Color
        }
    }
}

Describe "Invoke-RedirectedProcess" {
    BeforeEach {
        Mock Start-Process {
            [PSCustomObject]@{ ExitCode = 0 }
        }
    }

    It "passes arguments to Start-Process when supplied" {
        Invoke-RedirectedProcess `
            -ExecutablePath "tool.exe" `
            -Arguments "--flag" `
            -StandardOutputPath "stdout.txt" `
            -StandardErrorPath "stderr.txt"

        Should -Invoke -CommandName Start-Process -Times 1 -Exactly -Scope It -ParameterFilter {
            $FilePath -eq "tool.exe" -and
                $ArgumentList -eq "--flag" -and
                $RedirectStandardOutput -eq "stdout.txt" -and
                $RedirectStandardError -eq "stderr.txt" -and
                $Wait -and
                $PassThru -and
                $NoNewWindow
        }
    }

    It "omits ArgumentList when no arguments are supplied" {
        Invoke-RedirectedProcess `
            -ExecutablePath "tool.exe" `
            -StandardOutputPath "stdout.txt" `
            -StandardErrorPath "stderr.txt"

        Should -Invoke -CommandName Start-Process -Times 1 -Exactly -Scope It -ParameterFilter {
            $FilePath -eq "tool.exe" -and -not $ArgumentList
        }
    }
}

Describe "Invoke-ExecutableIfExist" {
    BeforeEach {
        Mock Write-LogMessage {}
        Mock Write-RedirectedProcessOutput {}
        Mock Invoke-RedirectedProcess {
            [PSCustomObject]@{ ExitCode = 0 }
        }
    }

    It "skips a missing executable" {
        $result = Invoke-ExecutableIfExist `
            -ExecutablePath (Join-Path $TestDrive "missing.exe") `
            -ExecutableName "Tool" `
            -MissingTarget "executable"

        $result | Should -Be 0
        Should -Invoke -CommandName Invoke-RedirectedProcess -Times 0 -Exactly -Scope It
        Should -Invoke -CommandName Write-LogMessage -Times 1 -Exactly -Scope It -ParameterFilter {
            $Message -like "*Skipped (executable not found)" -and
                $Level -eq "Information" -and
                $Color -eq "DarkGray"
        }
    }

    It "returns success and removes its temporary files" {
        $executablePath = Join-Path $TestDrive "tool.exe"
        New-Item -ItemType File -Path $executablePath | Out-Null
        $script:standardOutputPath = $null
        $script:standardErrorPath = $null
        Mock Invoke-RedirectedProcess {
            $script:standardOutputPath = $StandardOutputPath
            $script:standardErrorPath = $StandardErrorPath
            [PSCustomObject]@{ ExitCode = 0 }
        }

        $result = Invoke-ExecutableIfExist `
            -ExecutablePath $executablePath `
            -Arguments "--flag" `
            -Description "Running tool" `
            -Emoji "*" `
            -ExecutableName "Tool" `
            -MissingTarget "executable"

        $result | Should -Be 0
        Test-Path $script:standardOutputPath | Should -BeFalse
        Test-Path $script:standardErrorPath | Should -BeFalse
        Should -Invoke -CommandName Write-RedirectedProcessOutput -Times 2 -Exactly -Scope It
        Should -Invoke -CommandName Write-LogMessage -Times 1 -Exactly -Scope It -ParameterFilter {
            $Message -like "*Done" -and $Level -eq "Success"
        }
    }

    It "returns a non-zero exit code with the requested failure target" {
        $executablePath = Join-Path $TestDrive "tool.exe"
        New-Item -ItemType File -Path $executablePath -Force | Out-Null
        Mock Invoke-RedirectedProcess {
            [PSCustomObject]@{ ExitCode = 42 }
        }

        $result = Invoke-ExecutableIfExist `
            -ExecutablePath $executablePath `
            -ExecutableName "Script" `
            -MissingTarget "script" `
            -FailureTarget $executablePath

        $result | Should -Be 42
        Should -Invoke -CommandName Write-LogMessage -Times 1 -Exactly -Scope It -ParameterFilter {
            $Message -like "*Script exited with code 42: $executablePath" -and
                $Level -eq "Warning"
        }
    }

    It "returns a non-zero exit code without a failure target" {
        $executablePath = Join-Path $TestDrive "tool.exe"
        New-Item -ItemType File -Path $executablePath -Force | Out-Null
        Mock Invoke-RedirectedProcess {
            [PSCustomObject]@{ ExitCode = 7 }
        }

        $result = Invoke-ExecutableIfExist `
            -ExecutablePath $executablePath `
            -ExecutableName "Tool" `
            -MissingTarget "executable"

        $result | Should -Be 7
        Should -Invoke -CommandName Write-LogMessage -Times 1 -Exactly -Scope It -ParameterFilter {
            $Message -like "*Tool exited with code 7" -and
                $Level -eq "Warning"
        }
    }
}

Describe "Executable wrappers" {
    BeforeEach {
        Mock Invoke-ExecutableIfExist { 0 }
    }

    It "passes batch script details to the common runner" {
        $result = Invoke-ScriptIfExist `
            -ScriptPath "setup.bat" `
            -Arguments "install" `
            -Description "Installing" `
            -Emoji "*"

        $result | Should -Be 0
        Should -Invoke -CommandName Invoke-ExecutableIfExist -Times 1 -Exactly -Scope It -ParameterFilter {
            $ExecutablePath -eq "setup.bat" -and
                $Arguments -eq "install" -and
                $Description -eq "Installing" -and
                $Emoji -eq "*" -and
                $ExecutableName -eq "Script" -and
                $MissingTarget -eq "script" -and
                $FailureTarget -eq "setup.bat"
        }
    }

    It "passes the packaged Sunshine path to the common runner" {
        $script:RootDir = $TestDrive

        $result = Invoke-SunshineIfExist `
            -Arguments "--restore-nvprefs-undo" `
            -Description "Restoring" `
            -Emoji "*"

        $result | Should -Be 0
        Should -Invoke -CommandName Invoke-ExecutableIfExist -Times 1 -Exactly -Scope It -ParameterFilter {
            $ExecutablePath -eq (Join-Path $TestDrive "sunshine.exe") -and
                $Arguments -eq "--restore-nvprefs-undo" -and
                $Description -eq "Restoring" -and
                $Emoji -eq "*" -and
                $ExecutableName -eq "Sunshine" -and
                $MissingTarget -eq "executable" -and
                -not $FailureTarget
        }
    }
}
