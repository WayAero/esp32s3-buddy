param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"

$fontConverter = Join-Path $PSScriptRoot 'node_modules\lv_font_conv\lv_font_conv.js'
if (-not (Test-Path -LiteralPath $fontConverter)) {
    throw 'Font converter is missing. Run npm ci --prefix tools/fonts from the project root first.'
}

$sourceFont = Join-Path $ProjectRoot "components\third_party\lvgl__lvgl\scripts\built_in_font\SourceHanSansSC-Normal.otf"
$sourceRoots = @(
    (Join-Path $ProjectRoot "main"),
    (Join-Path $ProjectRoot "components")
)
$builtinOutput = Join-Path $ProjectRoot "main\fonts\ui_font_zh_14.c"
$dynamicOutput = Join-Path $ProjectRoot "storage\fonts\zh_cn_14.bin"
$dynamicFontVersion = 4
# 莞不在旧的 GB2312 常用字子集中，可用于拒绝旧版 storage 字库。
$dynamicFontV3Sentinel = [char]0x839E

if (-not (Test-Path -LiteralPath $sourceFont)) {
    throw "Source Han Sans font not found: $sourceFont"
}

$sourceFiles = @($sourceRoots | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -Recurse -File -Include *.c,*.h
} | Where-Object {
    $_.FullName -notmatch '[\\/]main[\\/]fonts[\\/]' -and
    $_.FullName -notmatch '[\\/]components[\\/]third_party[\\/]' -and
    $_.FullName -notmatch '[\\/](build|managed_components)[\\/]'
})
$fixedGlyphs = [System.Collections.Generic.HashSet[char]]::new()
foreach ($sourceFile in $sourceFiles) {
    $sourceText = Get-Content -LiteralPath $sourceFile.FullName -Raw
    foreach ($literal in [regex]::Matches($sourceText, '"(?:\\.|[^"\\])*"')) {
        foreach ($ch in $literal.Value.ToCharArray()) {
            if ([int]$ch -gt 127) {
                [void]$fixedGlyphs.Add($ch)
            }
        }
    }
}
# 大字集转 binfont 时少数字形未写入 cmap；让内置字体兜住这些字，
# 同时在下方校验中拒绝任何未被两种字体覆盖的字符。
foreach ($ch in '万口纟芈讠辁铈锝门颌饣马鱿鸟'.ToCharArray()) {
    [void]$fixedGlyphs.Add($ch)
}
# 动态审批、SSID 等文本常用标点；同字号内置字库也保留这些符号。
# 用码点避免 PowerShell 将中文引号解析为字符串定界符。
$punctuation = @(0xFF0C, 0x3002, 0xFF01, 0xFF1F, 0xFF1B, 0xFF1A, 0x3001, 0xFF08, 0xFF09, 0xFF3B, 0xFF3D, 0x3010, 0x3011, 0x300A, 0x300B, 0x3008, 0x3009, 0x300C, 0x300D, 0x300E, 0x300F, 0x201C, 0x201D, 0x2018, 0x2019, 0x2026, 0x2014, 0x2013, 0x00B7, 0x3000)
foreach ($code in $punctuation) { [void]$fixedGlyphs.Add([char]$code) }
$fixedSymbols = -join ($fixedGlyphs | Sort-Object { [int]$_ })
if ($fixedSymbols.Length -eq 0) {
    throw "No non-ASCII firmware string glyphs were found"
}

[Text.Encoding]::RegisterProvider([Text.CodePagesEncodingProvider]::Instance)
$gb2312 = [Text.Encoding]::GetEncoding(936)
$common = [System.Collections.Generic.HashSet[char]]::new()
for ($lead = 0xB0; $lead -le 0xF7; $lead++) {
    for ($trail = 0xA1; $trail -le 0xFE; $trail++) {
        $decoded = $gb2312.GetString([byte[]]@($lead, $trail))
        if ($decoded.Length -eq 1 -and
            $decoded[0] -ne [char]0xFFFD -and
            ([int]$decoded[0] -lt 0xE000 -or [int]$decoded[0] -gt 0xF8FF)) {
            [void]$common.Add($decoded[0])
        }
    }
}
foreach ($ch in $fixedSymbols.ToCharArray()) {
    [void]$common.Add($ch)
}
# 审批正文优先使用一套字体，ASCII 与中文均按 14px 生成。
for ($code = 32; $code -le 126; $code++) { [void]$common.Add([char]$code) }
[void]$common.Add($dynamicFontV3Sentinel)
$commonSymbols = -join ($common | Sort-Object { [int]$_ })

New-Item -ItemType Directory -Force (Split-Path $builtinOutput) | Out-Null
New-Item -ItemType Directory -Force (Split-Path $dynamicOutput) | Out-Null

function Add-StaticPrefix {
    param([string]$Source, [string]$Prefix)

    foreach ($name in @('glyph_bitmap', 'glyph_dsc', 'unicode_list_0', 'cmaps', 'font_dsc')) {
        $Source = $Source -replace "(?<!\.)\b$name\b", "${Prefix}_${name}"
    }
    return $Source
}

$generatedFonts = [System.Collections.Generic.List[string]]::new()
foreach ($size in @(14, 12, 18)) {
    $tempOutput = Join-Path ([IO.Path]::GetTempPath()) "ui_font_zh_${size}_$PID.c"
    $fallbackSize = if ($size -eq 18) { 14 } else { $size }
    try {
        & node $fontConverter `
            --size $size --bpp 2 --format lvgl `
            --font $sourceFont --symbols $fixedSymbols --no-kerning `
            --lv-include lvgl.h `
            --lv-font-name "ui_font_zh_$size" --lv-fallback "lv_font_montserrat_$fallbackSize" `
            -o $tempOutput
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to generate ${size}px built-in Chinese font"
        }
        $generated = (Get-Content -LiteralPath $tempOutput -Raw).Trim()
        # 转换器头部包含本机路径和临时 PID；去掉它使源码可重复生成。
        $generated = $generated -replace '(?m)^ \* Opts:.*\r?\n', ''
        if ($size -ne 14) {
            $generated = Add-StaticPrefix -Source $generated -Prefix "ui_font_zh_$size"
        }
        $generatedFonts.Add($generated)
    }
    finally {
        Remove-Item -LiteralPath $tempOutput -Force -ErrorAction SilentlyContinue
    }
}
[IO.File]::WriteAllText($builtinOutput,
                        (($generatedFonts -join "`n`n") + "`n"),
                        [Text.UTF8Encoding]::new($false))

$tempDynamicOutput = Join-Path (Split-Path -Parent $dynamicOutput) "zh_cn_14_$([guid]::NewGuid().ToString('N')).bin"
try {
    & node $fontConverter `
        --size 14 --bpp 2 --format bin `
        --font $sourceFont --symbols $commonSymbols --no-kerning `
        -o $tempDynamicOutput
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to generate storage Chinese font"
    }
    [IO.File]::Move($tempDynamicOutput, $dynamicOutput, $true)
}
finally {
    Remove-Item -LiteralPath $tempDynamicOutput -Force -ErrorAction SilentlyContinue
}

$builtinText = Get-Content -LiteralPath $builtinOutput -Raw
$builtinCodepoints = [System.Collections.Generic.HashSet[int]]::new()
foreach ($match in [regex]::Matches($builtinText, '/\* U\+([0-9A-Fa-f]+)')) {
    [void]$builtinCodepoints.Add([Convert]::ToInt32($match.Groups[1].Value, 16))
}
$missingBuiltin = @($fixedSymbols.ToCharArray() |
    Where-Object { -not $builtinCodepoints.Contains([int]$_) } |
    Sort-Object -Unique)
if ($missingBuiltin.Count -gt 0) {
    throw "Built-in font coverage failed; missing: $(-join $missingBuiltin)"
}

$fontBytes = [IO.File]::ReadAllBytes($dynamicOutput)
if ($fontBytes.Length -lt 48 -or [Text.Encoding]::ASCII.GetString($fontBytes, 4, 4) -ne 'head') {
    throw "Storage font header is invalid"
}
$fontSize = [BitConverter]::ToUInt16($fontBytes, 14)
if ($fontSize -ne 14) {
    throw "Storage font size mismatch: expected 14, got $fontSize"
}
# 与 ui_locale.c 的运行时校验保持一致，避免生成成功但加载时被拒绝。
$lineHeight = [BitConverter]::ToUInt16($fontBytes, 16) - [BitConverter]::ToInt16($fontBytes, 18)
if ($lineHeight -ne 17) { throw "Storage font line height mismatch: expected 17, got $lineHeight" }
$cmapOffset = [BitConverter]::ToUInt32($fontBytes, 0)
if ([Text.Encoding]::ASCII.GetString($fontBytes, $cmapOffset + 4, 4) -ne 'cmap') {
    throw "Storage font cmap is invalid"
}
$cmapCount = [BitConverter]::ToUInt32($fontBytes, $cmapOffset + 8)
$dynamicCodepoints = [System.Collections.Generic.HashSet[int]]::new()
for ($i = 0; $i -lt $cmapCount; $i++) {
    $table = $cmapOffset + 12 + ($i * 16)
    $dataOffset = [BitConverter]::ToUInt32($fontBytes, $table)
    $rangeStart = [BitConverter]::ToUInt32($fontBytes, $table + 4)
    $rangeLength = [BitConverter]::ToUInt16($fontBytes, $table + 8)
    $entryCount = [BitConverter]::ToUInt16($fontBytes, $table + 12)
    $format = $fontBytes[$table + 14]
    if ($format -eq 0) {
        for ($offset = 0; $offset -lt $entryCount; $offset++) {
            if ($fontBytes[$cmapOffset + $dataOffset + $offset] -ne 0) {
                [void]$dynamicCodepoints.Add([int]($rangeStart + $offset))
            }
        }
    }
    elseif ($format -eq 1 -or $format -eq 3) {
        for ($entry = 0; $entry -lt $entryCount; $entry++) {
            $offset = [BitConverter]::ToUInt16($fontBytes, $cmapOffset + $dataOffset + ($entry * 2))
            [void]$dynamicCodepoints.Add([int]($rangeStart + $offset))
        }
    }
    elseif ($format -eq 2) {
        for ($offset = 0; $offset -lt $rangeLength; $offset++) {
            [void]$dynamicCodepoints.Add([int]($rangeStart + $offset))
        }
    }
    else {
        throw "Unsupported storage font cmap format: $format"
    }
}
$missingDynamic = @($commonSymbols.ToCharArray() |
    Where-Object { -not $dynamicCodepoints.Contains([int]$_) -and
                    -not $builtinCodepoints.Contains([int]$_) } |
    Sort-Object -Unique)
if ($missingDynamic.Count -gt 0) {
    throw "Chinese font coverage failed; missing from storage and firmware: $(-join $missingDynamic)"
}
if (-not $dynamicCodepoints.Contains([int]$dynamicFontV3Sentinel)) {
    throw "Storage font v$dynamicFontVersion sentinel is missing from cmap"
}

Write-Host "Built-in fonts: 12/14/18px, $($fixedGlyphs.Count) firmware glyphs"
Write-Host "Storage font: 14px v$dynamicFontVersion, $($dynamicCodepoints.Count) glyphs"
Write-Host "Built-in output: $builtinOutput"
Write-Host "Storage output:  $dynamicOutput"
