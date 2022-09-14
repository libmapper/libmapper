Write-Host "Running all tests"
Write-Host $pwd
Get-ChildItem -Path $pwd\..\build\test\Release
Get-ChildItem -Path $pwd\..\build\test\Release\*.exe | ForEach-Object -Process {
    Write-Host "Running " $_.Fullname
    $proc = Start-Process $_.Fullname -ArgumentList "-t" -PassThru
    Write-Host "  started..."
    Wait-Process -InputObject $proc
    Write-Host "  complete"
    if ($proc.ExitCode -ne 0) {
        Write-Host "failed " $_.Fullname
        exit 1
    }
}
pause
