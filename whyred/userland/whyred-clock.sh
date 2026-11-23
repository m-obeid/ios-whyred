#!/bin/sh
# whyred: set the clock at boot from the PM660 RTC counter (kern.whyred_rtc, kernel 0996) plus a saved offset,
# then keep the offset fresh while the clock holds a real date (set by network time or by this script).
# Without it every boot starts in 1970 until Wi-Fi time sync; dasd saved a 1970 lastModulation and then
# recomputed widget budgets for ~20000 days, keeping coreduetd at a full core (2026-11-23).
F=/var/root/whyred_rtc_offset
VALID=1700000000

rtc() { /usr/sbin/sysctl -n kern.whyred_rtc 2>/dev/null; }

# Retry for a minute: at launchd start the RTC read or /var may not be ready yet.
i=0
while [ $i -lt 30 ]; do
	r=$(rtc)
	now=$(/usr/bin/date +%s)
	off=$(cat "$F" 2>/dev/null)
	if [ "$now" -ge "$VALID" ]; then
		echo "whyred-clock: clock already valid ($now)"
		break
	fi
	if [ "${r:--1}" -ge 0 ] 2>/dev/null && [ -n "$off" ]; then
		/usr/bin/date -u -s "@$((r + off))" >/dev/null
		echo "whyred-clock: set $(/usr/bin/date -u) from rtc $r, try $i"
		break
	fi
	echo "whyred-clock: try $i: rtc '$r' offset '$off' now $now"
	i=$((i + 1))
	sleep 2
done

while :; do
	r=$(rtc)
	now=$(/usr/bin/date +%s)
	if [ "${r:--1}" -ge 0 ] 2>/dev/null && [ "$now" -ge "$VALID" ]; then
		echo $((now - r)) > "$F.tmp" && mv "$F.tmp" "$F"
	fi
	sleep 300
done
