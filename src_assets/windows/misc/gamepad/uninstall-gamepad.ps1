# Use Get-CimInstance to find and uninstall Virtual Gamepad
$product = Get-CimInstance -ClassName Win32_Product -Filter "Name='ViGEm Bus Driver'"
if ($product) {
    Invoke-CimMethod -InputObject $product -MethodName Uninstall
    Write-Information "ViGEm Bus Driver uninstalled successfully"
} else {
    Write-Warning "ViGEm Bus Driver not found"
}
