#!/bin/sh
case "$1" in
--running)
systemctl list-units --type=service --state=running --no-legend --no-pager 2>/dev/null | while read -r line; do
name=$(echo "$line" | awk '{print $1}' | sed 's/\.service$//')
desc=$(echo "$line" | awk '{$1=$2=$3=$4=""; print $0}' | sed 's/^[[:space:]]*//')
echo "${name}|${desc}"
done
;;
--enabled)
systemctl list-unit-files --type=service --state=enabled --no-legend --no-pager 2>/dev/null | awk '{print $1}' | sed 's/\.service$//'
;;
--disabled)
systemctl list-unit-files --type=service --state=disabled --no-legend --no-pager 2>/dev/null | awk '{print $1}' | sed 's/\.service$//'
;;
--masked)
systemctl list-unit-files --type=service --state=masked --no-legend --no-pager 2>/dev/null | awk '{print $1}' | sed 's/\.service$//'
;;
--all)
systemctl list-unit-files --type=service --no-legend --no-pager 2>/dev/null | while read -r line; do
name=$(echo "$line" | awk '{print $1}' | sed 's/\.service$//')
state=$(echo "$line" | awk '{print $2}')
echo "${name}|${state}"
done
;;
esac

