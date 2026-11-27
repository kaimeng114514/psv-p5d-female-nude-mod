# Generates zh_font.h: 1bpp bitmap lines for the installer UI (Chinese).
Add-Type -AssemblyName System.Drawing

$lines = @(
  @{ t = "P5D 模组安装器";               s = 30 },
  @{ t = "单文件全自动版";               s = 20 },
  @{ t = "正在准备文件…";                s = 20 },
  @{ t = "正在加载解密模块…";            s = 20 },
  @{ t = "正在解密游戏启动文件，请稍候…"; s = 20 },
  @{ t = "正在重建补丁并写入 rePatch…";  s = 20 },
  @{ t = "安装完成！请启动游戏。";        s = 20 },
  @{ t = "安装失败！请查看日志。";        s = 20 },
  @{ t = "日志：ux0:data/P5DMod/p5d_installer_log.txt"; s = 16 },
  @{ t = "首次运行约需 1 分钟，请勿断电。"; s = 16 },
  @{ t = "按 X 键退出";                  s = 20 }
)

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("/* auto-generated Chinese UI bitmap lines (1bpp) */")
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("typedef struct { int w; int h; const unsigned int *bits; } ZhLine;")

$idx = 0
foreach ($ln in $lines) {
  $text = $ln.t
  $size = $ln.s
  $f = New-Object System.Drawing.Font("Microsoft YaHei", [single]$size, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
  $tmp = New-Object System.Drawing.Bitmap 1200, 80
  $g = [System.Drawing.Graphics]::FromImage($tmp)
  $g.Clear([System.Drawing.Color]::Black)
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
  $sf = [System.Drawing.StringFormat]::GenericTypographic
  $sf.FormatFlags = $sf.FormatFlags -bor [System.Drawing.StringFormatFlags]::NoClip
  $sz = $g.MeasureString($text, $f, 1200, $sf)
  $w = [int][Math]::Ceiling($sz.Width) + 2
  $h = [int][Math]::Ceiling($sz.Height) + 2
  $g.Dispose(); $tmp.Dispose()

  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::Black)
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
  $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
  $g.DrawString($text, $f, $white, 0, 0, $sf)
  $g.Dispose()

  $stride = [int]([Math]::Ceiling($w / 32.0))
  $bits = New-Object 'System.UInt32[]' ($stride * $h)
  for ($y = 0; $y -lt $h; $y++) {
    for ($x = 0; $x -lt $w; $x++) {
      $c = $bmp.GetPixel($x, $y)
      if (($c.R + $c.G + $c.B) -gt 96) {
        $bits[$y * $stride + [int][Math]::Floor($x / 32)] = $bits[$y * $stride + [int][Math]::Floor($x / 32)] -bor ([uint32](1) -shl (31 - ($x % 32)))
      }
    }
  }
  $bmp.Dispose(); $white.Dispose(); $f.Dispose()

  [void]$sb.AppendLine("static const unsigned int zh_${idx}_bits[] = {")
  $row = "  "
  for ($i = 0; $i -lt $bits.Length; $i++) {
    $row += ("0x{0:X8}u," -f $bits[$i])
    if ((($i + 1) % 8) -eq 0) { [void]$sb.AppendLine($row); $row = "  " }
  }
  if ($row.Length -gt 2) { [void]$sb.AppendLine($row) }
  [void]$sb.AppendLine("};")
  [void]$sb.AppendLine("static const ZhLine zh_lines_$idx = { $w, $h, zh_${idx}_bits };")
  $idx++
}

[void]$sb.AppendLine("static const ZhLine *const zh_lines[] = {")
for ($i = 0; $i -lt $idx; $i++) { [void]$sb.AppendLine("  &zh_lines_$i,") }
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("#define ZH_LINE_COUNT $idx")

$out = "C:\dev\p5d-vita-installer\src\zh_font.h"
[System.IO.File]::WriteAllText($out, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Host ("wrote " + $out + " lines=" + $idx)
