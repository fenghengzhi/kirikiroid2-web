#!/usr/bin/env bash
# debug-capture.sh — Capture screenshots + full console logs from a KrKr2 game run.
# Usage: ./tools/debug-capture.sh [url] [output_dir]
#
# Defaults:
#   url = http://localhost:8080/index.html?game=game.zip&entry=data.xp3
#   output_dir = /tmp/krkr2-debug-$(date +%s)

set -euo pipefail

URL="${1:-http://localhost:8080/index.html?game=game.zip&entry=data.xp3}"
OUTDIR="${2:-/tmp/krkr2-debug-$(date +%s)}"
mkdir -p "$OUTDIR"

echo "=== KrKr2 Debug Capture ==="
echo "URL: $URL"
echo "Output: $OUTDIR"
echo ""

# Kill any existing playwright session
playwright-cli close 2>/dev/null || true
sleep 0.5

# 1. Open blank page, inject log collector, then navigate
echo "[1/7] Opening browser with log collector..."
playwright-cli open "about:blank" 2>&1 | head -3

playwright-cli run-code "async page => {
  await page.addInitScript(() => {
    window.__allLogs = [];
    window.__logStartTime = Date.now();
    var origLog = console.log;
    var origWarn = console.warn;
    var origError = console.error;
    var origInfo = console.info;
    var origDebug = console.debug;
    function capture(level, args) {
      var elapsed = Date.now() - window.__logStartTime;
      var msg = Array.prototype.slice.call(args).map(function(a) {
        return typeof a === 'string' ? a : JSON.stringify(a);
      }).join(' ');
      window.__allLogs.push('[' + elapsed + 'ms] [' + level + '] ' + msg);
    }
    console.log = function() { capture('LOG', arguments); origLog.apply(console, arguments); };
    console.warn = function() { capture('WARN', arguments); origWarn.apply(console, arguments); };
    console.error = function() { capture('ERROR', arguments); origError.apply(console, arguments); };
    console.info = function() { capture('INFO', arguments); origInfo.apply(console, arguments); };
    console.debug = function() { capture('DEBUG', arguments); origDebug.apply(console, arguments); };
  });
  await page.goto('$URL');
}" 2>&1 | tail -3

# 2. Wait for engine init, capture loading screenshot
echo "[2/7] Waiting for engine init (10s)..."
sleep 10
playwright-cli screenshot --filename="$OUTDIR/01-loading.png" 2>&1 | head -2

# 3. Wait for ATTENTION screen
echo "[3/7] Waiting for ATTENTION screen (15s more)..."
sleep 15
playwright-cli screenshot --filename="$OUTDIR/02-attention.png" 2>&1 | head -2

# 4. Click to dismiss ATTENTION
echo "[4/7] Clicking to dismiss ATTENTION..."
playwright-cli run-code "async page => {
  const cdp = await page.context().newCDPSession(page);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: 640, y: 360 }] });
  await page.waitForTimeout(100);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
}" 2>&1 | head -2

# 5. Capture during logo animation
echo "[5/7] Capturing logo animation phase..."
sleep 2
playwright-cli screenshot --filename="$OUTDIR/03-logo-early.png" 2>&1 | head -2
sleep 3
playwright-cli screenshot --filename="$OUTDIR/04-logo-mid.png" 2>&1 | head -2
sleep 5
playwright-cli screenshot --filename="$OUTDIR/05-logo-late.png" 2>&1 | head -2

# 6. Wait for title screen
echo "[6/7] Waiting for title screen (10s more)..."
sleep 10
playwright-cli screenshot --filename="$OUTDIR/06-title.png" 2>&1 | head -2

# 7. Dump full logs
echo "[7/7] Dumping full console logs..."
playwright-cli run-code "async page => {
  const logs = await page.evaluate(() => {
    return (window.__allLogs || []).join('\n');
  });

  // Split into chunks and write via page title hack
  const CHUNK = 50000;
  const chunks = [];
  for (let i = 0; i < logs.length; i += CHUNK) {
    chunks.push(logs.substring(i, i + CHUNK));
  }
  // Store chunks for retrieval
  await page.evaluate((c) => { window.__logChunks = c; }, chunks);
  console.log('Log chunks: ' + chunks.length + ', total chars: ' + logs.length);
}" 2>&1 | tail -3

# Retrieve log chunks and write to file
NCHUNKS=$(playwright-cli eval "String((window.__logChunks || []).length)" 2>&1 | grep "^###" -A1 | tail -1 | tr -d '"' | xargs)
# Fallback: try direct dump
playwright-cli run-code "async page => {
  const logs = await page.evaluate(() => (window.__allLogs || []).join('\n'));
  // Write to a downloadable blob and trigger download... won't work headless.
  // Instead, just output summary.
  const lines = logs.split('\n');
  const errors = lines.filter(l => l.includes('[ERROR]'));
  const warns = lines.filter(l => l.includes('[WARN]'));
  const failed = lines.filter(l => l.includes('FAILED'));
  console.log('=== LOG SUMMARY ===');
  console.log('Total lines: ' + lines.length);
  console.log('Errors: ' + errors.length);
  console.log('Warnings: ' + warns.length);
  console.log('FAILED: ' + failed.length);
  console.log('NOT FOUND: ' + lines.filter(l => l.includes('NOT FOUND')).length);
}" 2>&1 | tail -8

# Dump logs in segments via eval (works with playwright-cli eval)
echo "" > "$OUTDIR/console.log"
TOTAL=$(playwright-cli eval "String((window.__allLogs || []).length)" 2>&1 | grep -oE '[0-9]+' | head -1)
echo "Total log entries: $TOTAL"

BATCH=500
OFFSET=0
while [ "$OFFSET" -lt "$TOTAL" ]; do
  CHUNK=$(playwright-cli eval "window.__allLogs.slice($OFFSET, $OFFSET + $BATCH).join('\n')" 2>&1 | sed -n '/^### Result/,/^###/{/^### Result/d;/^###/d;p;}' | sed 's/^"//;s/"$//' | sed 's/\\n/\n/g')
  echo "$CHUNK" >> "$OUTDIR/console.log"
  OFFSET=$((OFFSET + BATCH))
  # Progress
  PCT=$((OFFSET * 100 / TOTAL))
  printf "\r  Dumping logs: %d/%d (%d%%)" "$OFFSET" "$TOTAL" "$PCT"
done
echo ""

echo ""
echo "=== Capture Complete ==="
echo "Screenshots:"
ls -la "$OUTDIR"/*.png 2>/dev/null
echo ""
echo "Logs: $OUTDIR/console.log ($(wc -l < "$OUTDIR/console.log") lines)"
echo ""
echo "Quick search:"
echo "  grep 'FAILED' $OUTDIR/console.log | wc -l"
echo "  grep 'NOT FOUND' $OUTDIR/console.log | wc -l"
echo "  grep 'ERROR' $OUTDIR/console.log | wc -l"
