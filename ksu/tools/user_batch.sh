#!/system/bin/sh
# user_batch.sh — REA-NX9 post-root ops bundle. Invoked by ksu_loader.tmpl as
# `sh $RD/user_batch.sh` while uid0/kernel-domain + permissive, BEFORE the .ko
# load attempt (sig gate already passed). Everything here must be idempotent.
RD=$(dirname "$0")
echo "== user_batch start =="
id; getenforce

# --- 1) Protected packages the shell couldn't touch ---
# hnaps REMOVED from this list (codex audit): it is Honor's Adaptive
# Performance Service — per-app FPS/refresh/resolution scaling driven by
# PowerGenie; killing it breaks dynamic FPS and power tuning. Keep it alive.
for pkg in com.hihonor.id; do
  pm disable-user --user 0 $pkg
  pm suspend     --user 0 $pkg
  pm uninstall   --user 0 -k $pkg
  st=$(dumpsys package $pkg 2>/dev/null | grep -m1 -oE 'enabled=[0-9]+ .*installed=[a-z]+|installed=[a-z]+' )
  echo "STATE $pkg: $st"
done
# force-stop still-running instances of disabled packages
# DO-NOT-DISABLE (learned the hard way — silently breaks UI):
#   com.hihonor.systemmanager   — recents trash button (REQUEST_TRIM_ALL target)
#   com.hihonor.coauthservice   — FusionAuth backend: USB-mode picker + USB-debug toggle
#   com.hihonor.pcassistant     — hosts hisuite USB function (mode switching, debug toggle)
#   com.hihonor.ddmp            — drsp resource daemon
#   com.hihonor.securityserver / systemserver / powergenie / android / androidhnext
for pkg in com.hihonor.id \
           com.hihonor.nearby com.hihonor.trustcircle \
           com.hihonor.deviceauth com.hihonor.iconnect com.hihonor.hncloud \
           com.hihonor.ouc com.hihonor.remotepassword; do
  am force-stop $pkg 2>/dev/null
done
echo "== disable pass done =="

# --- 2) Settings hygiene ---
settings put global hn_system_update_auto_download 0
for ns in global secure system; do
  for kv in $(settings list $ns 2>/dev/null | grep -oE 'user_experience_hiview_[a-z_]+=[01]'); do
    settings put $ns "${kv%%=*}" 0 && echo "zeroed $ns ${kv%%=*}"
  done
done

# --- 3) Kill the framework-baked HiPush channel (iptables; per-boot) ---
if command -v iptables >/dev/null 2>&1; then
  iptables -I OUTPUT 1 -d 101.42.118.40 -j DROP
  iptables -I OUTPUT 1 -p tcp --dport 5223 -j DROP
  for d in trs-drru.push.hihonorcloud.com metrics-drru.dt.hihonorcloud.com apm-ru.yun.hihonor.com; do
    ip=$(ping -c 1 -W 2 $d 2>/dev/null | grep -oE '([0-9]+\.){3}[0-9]+' | head -1)
    [ -n "$ip" ] && { iptables -I OUTPUT 1 -d "$ip" -j DROP; echo "DROP $d -> $ip"; }
  done
  iptables -L OUTPUT -n | head -10
else
  echo "no iptables on device"
fi

# --- 4) KSU module skeleton: hosts sinkhole for Honor push/analytics ---
# (magic-mounts /system/etc/hosts on every boot once KSU is live)
MOD=/data/adb/modules/pushsink
mkdir -p $MOD/system/etc
cp /system/etc/hosts $MOD/system/etc/hosts 2>/dev/null || echo "127.0.0.1 localhost" > $MOD/system/etc/hosts
{
  echo "0.0.0.0 trs-drru.push.hihonorcloud.com"
  echo "0.0.0.0 metrics-drru.dt.hihonorcloud.com"
  echo "0.0.0.0 apm-ru.yun.hihonor.com"
  echo "0.0.0.0 push.hihonorcloud.com"
  echo "0.0.0.0 dt.hihonorcloud.com"
} >> $MOD/system/etc/hosts
printf 'id=pushsink\nname=Honor push sinkhole\nversion=v1\nversionCode=1\nauthor=ghostlock\ndescription=hosts sinkhole for hihonorcloud push/telemetry endpoints\n' > $MOD/module.prop
chmod -R a+r $MOD

# --- 5) Evidence + material for offline analysis ---
mkdir -p $RD/dumps && chmod 755 $RD/dumps
pm list packages -d > $RD/dumps/disabled_after.txt 2>/dev/null
pm list packages > $RD/dumps/pkg_all_after.txt 2>/dev/null
cat /proc/net/tcp6 /proc/net/tcp > $RD/dumps/sock_after.txt 2>/dev/null
cp /system/framework/framework-res.apk $RD/dumps/ 2>/dev/null
pm path com.android.settings 2>/dev/null | sed 's/^package://' | while read p; do cp "$p" $RD/dumps/ 2>/dev/null; done
for p in boot_a boot vendor_boot_a vendor_boot dtbo_a dtbo vbmeta_a vbmeta init_boot_a init_boot; do
  for b in /dev/block/by-name/$p /dev/block/bootdevice/by-name/$p; do
    t=$(readlink -f $b 2>/dev/null)
    if [ -b "$t" ]; then
      dd if=$t of=$RD/dumps/$p.img bs=1m 2>/dev/null && echo "dump $p <- $t ($(wc -c < $RD/dumps/$p.img)B)" && break
    fi
  done
done
chmod -R a+rX $RD/dumps 2>/dev/null
echo "== user_batch done =="
