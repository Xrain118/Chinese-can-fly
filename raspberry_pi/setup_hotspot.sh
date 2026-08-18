#!/usr/bin/env bash
set -euo pipefail

connection_name="${1:-UGV-Hotspot}"
ssid="${2:-UGV-Rescue}"
password="${3:-}"
interface="${4:-wlan0}"

if [[ ${#password} -lt 8 ]]; then
  echo "Usage: $0 [connection-name] [ssid] <password-at-least-8-chars> [interface]" >&2
  exit 2
fi

if nmcli -t -f NAME connection show | grep -Fxq "$connection_name"; then
  nmcli connection delete "$connection_name"
fi

nmcli device wifi hotspot \
  ifname "$interface" \
  con-name "$connection_name" \
  ssid "$ssid" \
  password "$password"
nmcli connection modify "$connection_name" \
  connection.autoconnect yes \
  ipv4.method shared \
  ipv6.method disabled
nmcli connection up "$connection_name"
