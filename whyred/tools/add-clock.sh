#!/bin/bash
# Install whyred-clock.sh on the running iOS (ssh root@192.168.7.2) as launchd job com.whyred.clock.
# Like add-ssh.sh, the job goes into launchd's cache /System/Library/xpc/launchd.plist (a plain
# LaunchDaemons file is ignored). A backup of the cache is kept as /var/root/launchd.plist.orig.
# Takes effect at the next boot. Needs kernel 0996 or later (kern.whyred_rtc).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PHONE=${PHONE:-root@192.168.7.2}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

ssh "$PHONE" 'test -f /var/root/launchd.plist.orig || cp -p /System/Library/xpc/launchd.plist /var/root/launchd.plist.orig'
ssh "$PHONE" 'cat /System/Library/xpc/launchd.plist' > "$work/launchd.plist"
python3 - "$work/launchd.plist" "$work/job.plist" <<'EOF'
import plistlib, sys
job = {
    'Label': 'com.whyred.clock',
    'ProgramArguments': ['/bin/sh', '/usr/libexec/whyred-clock.sh'],
    'RunAtLoad': True,
    'KeepAlive': True,
    'UserName': 'root',
    'LimitLoadToSessionType': 'System',
    'EnableTransactions': False,
    'StandardOutPath': '/private/var/log/whyred-clock.log',
    'StandardErrorPath': '/private/var/log/whyred-clock.log',
}
key = '/System/Library/LaunchDaemons/com.whyred.clock.plist'
plist = plistlib.load(open(sys.argv[1], 'rb'))
plist['LaunchDaemons'][key] = job
plistlib.dump(plist, open(sys.argv[1], 'wb'), fmt=plistlib.FMT_BINARY)
plistlib.dump(job, open(sys.argv[2], 'wb'))
print('launchd.plist: added', job['Label'])
EOF
ssh "$PHONE" 'cat > /usr/libexec/whyred-clock.sh && chmod 755 /usr/libexec/whyred-clock.sh' < "$ROOT/whyred/userland/whyred-clock.sh"
ssh "$PHONE" 'cat > /System/Library/LaunchDaemons/com.whyred.clock.plist && chmod 644 /System/Library/LaunchDaemons/com.whyred.clock.plist' < "$work/job.plist"
ssh "$PHONE" 'cat > /System/Library/xpc/launchd.plist.new && chown root:wheel /System/Library/xpc/launchd.plist.new && chmod 644 /System/Library/xpc/launchd.plist.new && mv /System/Library/xpc/launchd.plist.new /System/Library/xpc/launchd.plist' < "$work/launchd.plist"
echo "installed; reboot to start com.whyred.clock"
