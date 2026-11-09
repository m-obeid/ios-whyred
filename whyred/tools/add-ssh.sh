#!/bin/bash
# Add a userland (palera1n rootful Procursus bootstrap, cfver 1800) and dropbear on port 22 to the
# iOS root image on the laptop, then push it with whyred/tools/push-root.sh.
#   whyred/tools/add-ssh.sh [IMAGE]       (run as your user: it asks for sudo; default whyred/ipsw/ios16-root.img)
# Files that already exist in the iOS root are never overwritten (listed instead).
# Login: root with your ~/.ssh/id_ed25519.pub, no passwords. Needs whyred/tools/fetch-ssh-debs.py first.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMG=${1:-$ROOT/whyred/ipsw/ios16-root.img}
SSH=$ROOT/whyred/ipsw/ssh
PUB=${PUB:-$HOME/.ssh/id_ed25519.pub}
[ -f "$SSH/bootstrap-1800.tar.zst" ] || { echo "run whyred/tools/fetch-ssh-debs.py first" >&2; exit 1; }
[ -f "$PUB" ] || { echo "no $PUB" >&2; exit 1; }

# Each deb's data archive, unpacked from the .deb (ar) as your user.
stage=$(mktemp -d)
for d in "$SSH"/*.deb; do
	x=$stage/$(basename "$d" .deb)
	mkdir -p "$x" && (cd "$x" && ar x "$d")
done

sudo -v
mnt=$(mktemp -d)
sudo mount -t hfsplus -o loop,force "$IMG" "$mnt"
cleanup() { sudo umount "$mnt" 2>/dev/null || true; rmdir "$mnt" 2>/dev/null || true; rm -rf "$stage"; }
trap cleanup EXIT
[ -f "$mnt/System/Library/xpc/launchd.plist" ] || { echo "not an iOS root" >&2; exit 1; }

# Conflicts: files in the bootstrap that the iOS root already has (kept, not overwritten).
zstd -dc "$SSH/bootstrap-1800.tar.zst" | tar tf - | grep -v '/$' | sed 's|^\./||' > "$stage/list"
for t in "$stage"/*/data.tar.*; do tar tf "$t" | grep -v '/$' | sed 's|^\./||' >> "$stage/list"; done
n=0
while IFS= read -r f; do
	if sudo test -e "$mnt/$f" || sudo test -L "$mnt/$f"; then
		[ $n -lt 15 ] && echo "keeping iOS file: /$f"
		n=$((n + 1))
	fi
done < "$stage/list"
echo "$n bootstrap/deb files already exist in the iOS root and are kept"

# --keep-directory-symlink: /etc, /var, /tmp are symlinks into /private in iOS.
zstd -dc "$SSH/bootstrap-1800.tar.zst" | sudo tar -x -C "$mnt" -p --numeric-owner \
	--keep-directory-symlink --skip-old-files
for t in "$stage"/*/data.tar.*; do
	sudo tar -x -C "$mnt" -p --numeric-owner --keep-directory-symlink --skip-old-files -f "$t"
done
sudo rm -f "$mnt/prep_bootstrap.sh"

# root's key, host key directory, log
sudo install -d -m 0700 -o 0 -g 0 "$mnt/private/var/root/.ssh"
sudo install -m 0600 -o 0 -g 0 "$PUB" "$mnt/private/var/root/.ssh/authorized_keys"
sudo install -d -m 0755 -o 0 -g 0 "$mnt/private/etc/dropbear" "$mnt/private/var/log"

# launchd on iOS starts daemons from its compiled list, and only for jobs keyed under
# /System/Library (a /Library/LaunchDaemons key was ignored on the first boot).
sudo python3 - "$mnt" <<'EOF'
import os, plistlib, sys
root = sys.argv[1]
job = {
    'Label': 'com.whyred.dropbear',
    'ProgramArguments': ['/usr/sbin/dropbear', '-F', '-R', '-E', '-s', '-p', '22'],
    'RunAtLoad': True,
    'KeepAlive': True,
    'UserName': 'root',
    'POSIXSpawnType': 'Interactive',
    'LimitLoadToSessionType': 'System',
    'EnableTransactions': False,
    'StandardErrorPath': '/private/var/log/dropbear.log',
}
key = '/System/Library/LaunchDaemons/com.whyred.dropbear.plist'
old = '/Library/LaunchDaemons/com.whyred.dropbear.plist'
path = os.path.join(root, 'System/Library/xpc/launchd.plist')
st = os.stat(path)
plist = plistlib.load(open(path, 'rb'))
plist['LaunchDaemons'].pop(old, None)
plist['LaunchDaemons'][key] = job
plistlib.dump(plist, open(path, 'wb'), fmt=plistlib.FMT_BINARY)
os.chown(path, st.st_uid, st.st_gid)
os.chmod(path, st.st_mode & 0o7777)
for k in (old, key):
    try:
        os.remove(os.path.join(root, k.lstrip('/')))
    except FileNotFoundError:
        pass
plistlib.dump(job, open(os.path.join(root, key.lstrip('/')), 'wb'))
os.chmod(os.path.join(root, key.lstrip('/')), 0o644)
print('launchd.plist: added', job['Label'], ' '.join(job['ProgramArguments']))
EOF

for f in usr/sbin/dropbear bin/sh usr/lib/libiosexec.1.dylib usr/lib/libtomcrypt.1.dylib private/var/root/.ssh/authorized_keys; do
	sudo stat -c '%a %U %s /%n' "$mnt/$f" 2>&1 | sed "s|$mnt/||"
done
sudo umount "$mnt"
trap 'rmdir "$mnt" 2>/dev/null; rm -rf "$stage"' EXIT
sudo fsck.hfsplus -f -n "$IMG" | tail -1 || true
echo "done. Next: whyred/tools/push-root.sh"
