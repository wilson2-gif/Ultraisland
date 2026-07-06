# ═════════════════════════════════════════════════════════════════════════════
#  build_msix.ps1 — Packaging MSIX d'UltraIsland
#
#  Ce que fait le script :
#   1. Localise makeappx/signtool dans le Windows SDK
#   2. Prépare le layout (exe + logos générés + AppxManifest.xml)
#   3. Génère le paquet  packaging\out\UltraIsland.msix
#   4. Le signe avec un certificat de test auto-signé « CN=Ultraisland Dev »
#      (créé dans le magasin CurrentUser\My s'il n'existe pas)
#
#  INSTALLATION (une fois le .msix produit) :
#   .\build_msix.ps1 -Install     ← à lancer dans un PowerShell ADMIN :
#      importe le certificat dans LocalMachine\TrustedPeople puis
#      installe le paquet (Add-AppxPackage).
#
#  Après installation : lancer « UltraIsland » depuis le menu Démarrer, puis
#  autoriser l'accès aux notifications dans
#  Paramètres > Confidentialité et sécurité > Notifications.
# ═════════════════════════════════════════════════════════════════════════════
param(
    [ValidateSet("Release", "Debug", "Auto")]
    [string]$Config = "Auto",
    [switch]$Install,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
$root   = Split-Path $PSScriptRoot -Parent
$outDir = Join-Path $PSScriptRoot "out"
$layout = Join-Path $PSScriptRoot "_layout"
$msix   = Join-Path $outDir "UltraIsland.msix"
$subject = "CN=Ultraisland Dev"

# ── Mode -Uninstall : retire la version installée (ADMIN requis) ────────────
if ($Uninstall) {
    $existing = Get-AppxPackage -Name "Ultraisland.App" -ErrorAction SilentlyContinue
    if (-not $existing) { Write-Host "[INFO] Aucun paquet UltraIsland installé." -ForegroundColor Yellow; exit 0 }
    Get-Process WindowsDynamicIsland -ErrorAction SilentlyContinue | Stop-Process -Force
    Remove-AppxPackage -Package $existing.PackageFullName
    Write-Host "[OK] $($existing.PackageFullName) désinstallé." -ForegroundColor Green
    exit 0
}

# ── Mode -Install : trust du certificat + installation (ADMIN requis) ────────
if ($Install) {
    $isAdmin = ([Security.Principal.WindowsPrincipal] `
        [Security.Principal.WindowsIdentity]::GetCurrent()
        ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host "ERREUR : -Install exige un PowerShell ADMINISTRATEUR." -ForegroundColor Red
        exit 1
    }
    if (-not (Test-Path $msix)) {
        Write-Host "ERREUR : $msix introuvable. Lancez d'abord le script sans -Install." -ForegroundColor Red
        exit 1
    }
    $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object Subject -eq $subject | Select-Object -First 1
    if (-not $cert) { Write-Host "ERREUR : certificat '$subject' introuvable." -ForegroundColor Red; exit 1 }

    $cerPath = Join-Path $outDir "UltraIsland-dev.cer"
    Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null
    Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
    Write-Host "[OK] Certificat de test approuvé (LocalMachine\TrustedPeople)" -ForegroundColor Green

    # ── Installation robuste : tente ForceApplicationShutdown + ForceUpdateFromAnyVersion
    # Si le paquet est DÉJÀ installé à la même version → on le retire d'abord
    try {
        Add-AppxPackage -Path $msix -ForceApplicationShutdown -ForceUpdateFromAnyVersion -ErrorAction Stop
    } catch {
        $existing = Get-AppxPackage -Name "Ultraisland.App" -ErrorAction SilentlyContinue
        if ($existing) {
            Write-Host "[INFO] Version identique déjà installée ($($existing.Version)) — remplacement forcé…" -ForegroundColor Yellow
            # Stop des instances en cours (aucune erreur si absent)
            Get-Process WindowsDynamicIsland -ErrorAction SilentlyContinue | Stop-Process -Force
            Remove-AppxPackage -Package $existing.PackageFullName
            Start-Sleep -Milliseconds 500
            Add-AppxPackage -Path $msix -ForceApplicationShutdown
        } else {
            throw
        }
    }
    Write-Host "[OK] UltraIsland installé en MSIX !" -ForegroundColor Green
    Write-Host ""
    Write-Host "Prochaines étapes :" -ForegroundColor Cyan
    Write-Host "  1. Lancez « UltraIsland » depuis le menu Démarrer (PAS l'exe du repo)"
    Write-Host "  2. Paramètres > Confidentialité et sécurité > Notifications"
    Write-Host "     > Autoriser l'accès aux notifications pour UltraIsland"
    Write-Host "  3. Les vraies notifications s'afficheront alors dans l'encoche."
    exit 0
}

# ── 1. Outils SDK ─────────────────────────────────────────────────────────────
function Find-SdkTool([string]$name) {
    $kits = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    if (-not (Test-Path $kits)) { $kits = "$env:ProgramFiles\Windows Kits\10\bin" }
    $tool = Get-ChildItem "$kits\*\x64\$name" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $tool) { throw "$name introuvable — installez le Windows SDK." }
    return $tool.FullName
}
$makeappx = Find-SdkTool "makeappx.exe"
$signtool = Find-SdkTool "signtool.exe"
Write-Host "[OK] makeappx : $makeappx"
Write-Host "[OK] signtool : $signtool"

# ── 2. Exe source ─────────────────────────────────────────────────────────────
if ($Config -eq "Auto") {
    $Config = if (Test-Path "$root\x64\Release\WindowsDynamicIsland.exe") { "Release" } else { "Debug" }
}
$exe = "$root\x64\$Config\WindowsDynamicIsland.exe"
if (-not (Test-Path $exe)) { throw "Exe introuvable : $exe — compilez d'abord ($Config x64)." }
Write-Host "[OK] Exe ($Config) : $exe"

# ── 3. Layout : exe + manifeste + logos générés ───────────────────────────────
Remove-Item $layout -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$layout\Assets" | Out-Null
Copy-Item $exe "$layout\WindowsDynamicIsland.exe"
Copy-Item "$PSScriptRoot\AppxManifest.xml" "$layout\AppxManifest.xml"

# Logos : pilule sombre sur fond transparent (System.Drawing, zéro asset)
Add-Type -AssemblyName System.Drawing
function New-PillLogo([int]$size, [string]$path) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $w = [int]($size * 0.78); $h = [int]($size * 0.34)
    $x = [int](($size - $w) / 2); $y = [int](($size - $h) / 2)
    $r = [int]($h / 2)
    $gp = New-Object System.Drawing.Drawing2D.GraphicsPath
    $gp.AddArc($x, $y, $r*2, $r*2, 90, 180)
    $gp.AddArc($x + $w - $r*2, $y, $r*2, $r*2, 270, 180)
    $gp.CloseFigure()
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 18, 18, 24))
    $pen   = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(90, 255, 255, 255)), ([Math]::Max(1, $size / 72))
    $g.FillPath($brush, $gp); $g.DrawPath($pen, $gp)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}
New-PillLogo 150 "$layout\Assets\Square150x150Logo.png"
New-PillLogo 44  "$layout\Assets\Square44x44Logo.png"
New-PillLogo 50  "$layout\Assets\StoreLogo.png"
Write-Host "[OK] Layout préparé : $layout"

# ── 4. Pack ───────────────────────────────────────────────────────────────────
New-Item -ItemType Directory -Force $outDir | Out-Null
Remove-Item $msix -Force -ErrorAction SilentlyContinue
& $makeappx pack /o /d $layout /p $msix | Out-Null
if (-not (Test-Path $msix)) { throw "makeappx a échoué." }
Write-Host "[OK] Paquet : $msix"

# ── 5. Certificat de test + signature ────────────────────────────────────────
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object Subject -eq $subject | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type Custom -Subject $subject `
        -KeyUsage DigitalSignature -FriendlyName "UltraIsland dev (test MSIX)" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
    Write-Host "[OK] Certificat de test créé : $($cert.Thumbprint)"
} else {
    Write-Host "[OK] Certificat de test existant : $($cert.Thumbprint)"
}
& $signtool sign /fd SHA256 /sha1 $cert.Thumbprint $msix | Out-Null
Write-Host "[OK] Paquet signé." -ForegroundColor Green

Write-Host ""
Write-Host "════════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "  MSIX prêt : $msix"
Write-Host "  Pour INSTALLER (PowerShell ADMIN) :"
Write-Host "     cd packaging ; .\build_msix.ps1 -Install"
Write-Host "════════════════════════════════════════════════════════════" -ForegroundColor Cyan
