# Setup: adds KoEnJa to the user's Korean and Japanese keyboards. With neither language it adds Korean
# with KoEnJa as its only keyboard and exits 2 (the finish page says so).
# Uninstall (-Remove): takes KoEnJa out, and a language left with no keyboard (the Korean setup added).
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
if ($added) { exit 2 }
