# Path to the SyncTestSend executable
$SyncTestSendExe = ".\Examples\Bin\x64\Debug\SyncTestSend.x64.exe"

$FolderPath = "."

# Check if a folder path is provided as a command-line argument
if ($args.Count -gt 0) {
    $FolderPath = $args[0]
}

# Get all JSON configuration files in the specified folder
$ConfigFiles = Get-ChildItem -Path $FolderPath -Filter "*.cfg"


# Check if the executable exists
if (-Not (Test-Path $SyncTestSendExe)) {
    Write-Host "Error: $SyncTestSendExe not found." -ForegroundColor Red
    exit 1
}
# Store the list of subprocesses
$SubProcesses = @()

# Register an event to handle script termination
Register-EngineEvent -SourceIdentifier "ScriptTermination" -Action {
    Write-Host "Terminating subprocesses..." -ForegroundColor Yellow
    foreach ($Process in $SubProcesses) {
        if ($Process -and !$Process.HasExited) {
            Stop-Process -Id $Process.Id -Force
            Write-Host "Terminated process with ID: $($Process.Id)"
        }
    }
    Unregister-Event -SourceIdentifier "ScriptTermination"
}


# Iterate over each JSON configuration file and run the executable
foreach ($ConfigFile in $ConfigFiles) {
    Write-Host "Running $SyncTestSendExe with $($ConfigFile.Name)..."

    # Run the executable with the -config argument
    $Process = Start-Process -FilePath $SyncTestSendExe `
                             -ArgumentList "-config=$($ConfigFile.BaseName).cfg" `
                             -NoNewWindow `
                             -PassThru `
                             -RedirectStandardOutput "output_$($ConfigFile.BaseName).log" `
                             -RedirectStandardError "error_$($ConfigFile.BaseName).log"
    
    $SubProcesses += $Process
}

while ($true) {
    Start-Sleep -Seconds 1
}

# Cleanup: Unregister the termination event
Unregister-Event -SourceIdentifier "ScriptTermination"

Write-Host "All tests completed."
