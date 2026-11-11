param([string]$OutputDirectory = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function New-MarkBitmap([int]$Size) {
    $bitmap = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.Clear([System.Drawing.Color]::FromArgb(8, 8, 8))
    $graphics.ScaleTransform($Size / 256.0, $Size / 256.0)
    $gold = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(212, 175, 112), 15)
    $gold.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $gold.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $gold.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddBezier(190,57,145,20,95,36,75,65)
    $path.AddBezier(75,65,50,101,84,130,129,144)
    $path.AddBezier(129,144,169,156,211,172,183,209)
    $path.AddBezier(183,209,155,246,91,229,57,198)
    $graphics.DrawPath($gold, $path)
    $echo = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(226, 197, 142), 5)
    $echo.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $echo.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $graphics.DrawBezier($echo, 50,110,42,92,49,71,65,57)
    $graphics.DrawBezier($echo, 206,145,213,163,206,183,191,197)
    $graphics.Dispose(); $gold.Dispose(); $echo.Dispose(); $path.Dispose()
    return $bitmap
}

$pngs = @()
foreach ($size in @(16,24,32,48,64,128,256)) {
    $bitmap = New-MarkBitmap $size
    $path = Join-Path $OutputDirectory ("sonicsync-icon-{0}.png" -f $size)
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    $pngs += ,([pscustomobject]@{ Size=$size; Bytes=[System.IO.File]::ReadAllBytes($path) })
}

$icoPath = Join-Path $OutputDirectory 'sonicsync.ico'
$stream = [System.IO.File]::Create($icoPath)
$writer = [System.IO.BinaryWriter]::new($stream)
$writer.Write([UInt16]0); $writer.Write([UInt16]1); $writer.Write([UInt16]$pngs.Count)
$offset = 6 + 16 * $pngs.Count
foreach ($png in $pngs) {
    $dimension = if ($png.Size -eq 256) { [byte]0 } else { [byte]$png.Size }
    $writer.Write($dimension); $writer.Write($dimension); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([UInt16]1); $writer.Write([UInt16]32); $writer.Write([UInt32]$png.Bytes.Length); $writer.Write([UInt32]$offset)
    $offset += $png.Bytes.Length
}
foreach ($png in $pngs) { $writer.Write($png.Bytes) }
$writer.Dispose(); $stream.Dispose()

$bitmap = New-MarkBitmap 1024
$bitmap.Save((Join-Path $OutputDirectory 'sonicsync-mark-1024.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Host "Generated multi-resolution icon and 1024px mark in $OutputDirectory"
