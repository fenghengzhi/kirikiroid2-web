#!/bin/zsh

# 使用方式：
# ./press-enter.zsh "2026-07-12 08:30:00"
# ./press-enter.zsh "2026-07-12 08:30:00" "Google Chrome"

TARGET_TIME="$1"
TARGET_APP="$2"

if [[ -z "$TARGET_TIME" ]]; then
  echo '用法：'
  echo '  ./press-enter.zsh "YYYY-MM-DD HH:MM:SS"'
  echo '  ./press-enter.zsh "YYYY-MM-DD HH:MM:SS" "应用名称"'
  exit 1
fi

TARGET_TIMESTAMP=$(date -j -f "%Y-%m-%d %H:%M:%S" "$TARGET_TIME" "+%s" 2>/dev/null)

if [[ -z "$TARGET_TIMESTAMP" ]]; then
  echo "时间格式错误，应为：YYYY-MM-DD HH:MM:SS"
  exit 1
fi

CURRENT_TIMESTAMP=$(date "+%s")
WAIT_SECONDS=$((TARGET_TIMESTAMP - CURRENT_TIMESTAMP))

if (( WAIT_SECONDS <= 0 )); then
  echo "指定时间已经过去：$TARGET_TIME"
  exit 1
fi

echo "将在 $TARGET_TIME 按下回车，剩余 $WAIT_SECONDS 秒。"

# 防止等待期间 Mac 自动休眠
caffeinate -i -w $$ &
CAFFEINATE_PID=$!

sleep "$WAIT_SECONDS"

if [[ -n "$TARGET_APP" ]]; then
  osascript <<EOF
tell application "$TARGET_APP" to activate
delay 0.5
tell application "System Events"
    key code 36
end tell
EOF
else
  osascript <<EOF
tell application "System Events"
    key code 36
end tell
EOF
fi

kill "$CAFFEINATE_PID" 2>/dev/null

echo "已按下回车。"