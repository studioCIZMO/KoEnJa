# Setup: adds KoEnJa to the user's Korean and Japanese keyboards. With neither language it adds Korean
# with KoEnJa as its only keyboard and exits 2 (the finish page says so).
# Uninstall (-Remove): takes KoEnJa out, and a language left with no keyboard (the Korean setup added).
# The uninstaller runs elevated, maybe as another account than the one Setup added KoEnJa for, and
# Get-WinUserLanguageList is only the running account's: from every other user's list (User Profile
# in their registry hive, which that list is read from) only KoEnJa's own entry goes. Their languages
# stay, even one Setup added: nothing says it wasn't theirs. A hive that can't be unloaded is
# reported (exit 3).
param([switch]$Remove)
$tip = '{113C852D-C4E7-4C6E-B713-3F79874DE867}{56DCC211-5E23-42CE-9CF1-20EBA011028E}'
$list = Get-WinUserLanguageList
$added = -not $Remove -and -not ($list | Where-Object { $_.LanguageTag -like 'ko*' -or $_.LanguageTag -like 'ja*' })
if ($added) {
    $list.Add('ko-KR')
    $list[$list.Count - 1].InputMethodTips.Clear()
}
foreach ($language in @($list)) {
    $t = if ($language.LanguageTag -like 'ja*') { "0411:$tip" } elseif ($language.LanguageTag -like 'ko*') { "0412:$tip" }
    if (-not $t) { continue }
    if (-not $Remove) {
        if (-not $language.InputMethodTips.Contains($t)) { $language.InputMethodTips.Add($t) }
    } elseif ($language.InputMethodTips.Remove($t) -and $language.InputMethodTips.Count -eq 0 -and $list.Count -gt 1) {
        [void]$list.Remove($language)
    }
}
Set-WinUserLanguageList $list -Force
if ($Remove) {
    $self = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $users = [Microsoft.Win32.Registry]::Users
    $failed = $false
    # Every key is closed as soon as it has been used: an open one keeps a loaded hive from unloading.
    $paths = @{}
    $profileList = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList')
    try {
        foreach ($sid in $profileList.GetSubKeyNames() -match '^S-1-(5-21|12-1)-') { # local and domain, Entra ID
            $key = $profileList.OpenSubKey($sid)
            try { $paths[$sid] = $key.GetValue('ProfileImagePath') } finally { $key.Dispose() }
        }
    } finally { $profileList.Dispose() }
    foreach ($sid in $paths.Keys) {
        if ($sid -eq $self) { continue }
        # A user not signed in has no hive loaded: theirs is loaded while it is edited.
        $hive = $sid; $loaded = $null
        $key = $users.OpenSubKey($sid)
        if ($key) { $key.Dispose() } else {
            $dat = if ($paths[$sid]) { Join-Path $paths[$sid] 'NTUSER.DAT' }
            if (-not $dat -or -not (Test-Path -LiteralPath $dat)) { continue }
            $hive = "KoEnJa-$sid"
            $out = reg.exe load "HKU\$hive" $dat 2>&1
            if ($LASTEXITCODE) { Write-Warning "KoEnJa: couldn't load $sid's registry (reg load $LASTEXITCODE): $out"; $failed = $true; continue }
            $loaded = $hive
        }
        try {
            $root = $users.OpenSubKey("$hive\Control Panel\International\User Profile")
            if ($root) {
                try {
                    foreach ($tag in $root.GetSubKeyNames()) {
                        $key = $root.OpenSubKey($tag, $true)
                        try { foreach ($name in @($key.GetValueNames()) -like "????:$tip") { $key.DeleteValue($name) } }
                        finally { $key.Dispose() }
                    }
                } finally { $root.Dispose() }
            }
        } catch {
            Write-Warning "KoEnJa: couldn't edit $sid's keyboards: $_"; $failed = $true
        } finally {
            if ($loaded) {
                [gc]::Collect(); [gc]::WaitForPendingFinalizers()
                $out = reg.exe unload "HKU\$loaded" 2>&1
                if ($LASTEXITCODE) { Write-Warning "KoEnJa: couldn't unload $sid's registry HKU\$loaded (reg unload $LASTEXITCODE): $out"; $failed = $true }
            }
        }
    }
    if ($failed) { exit 3 }
}
if ($added) { exit 2 }
