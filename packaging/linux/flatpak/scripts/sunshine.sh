#!/bin/sh

PORT=47990

if ! curl -k https://localhost:$PORT > /dev/null 2>&1; then
  (sleep 3 && xdg-open https://localhost:$PORT) &
  exec sunshine "$@"
else
  echo "Sunshine is already running, opening the web interface..."
  xdg-open https://localhost:$PORT
fi
