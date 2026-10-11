#!/bin/sh
export PATH=/usr/bin:/usr/sbin:/bin:/sbin
export HOME=/root LIBSEAT_BACKEND=noop XDG_RUNTIME_DIR=/tmp DENIA_NO_PREDECESSOR=1
export LD_PRELOAD=/usr/lib/libdenialshim.so DENIA_RENDER_AUDIT=1
export GALLIUM_DRIVER=zink MESA_LOADER_DRIVER_OVERRIDE=zink
export MESA_GLES_VERSION_OVERRIDE=3.2
mount -t proc proc /proc 2>/dev/null
rm -f /tmp/deniald-alive.log /tmp/snap /tmp/snap.txt
{
echo "== e2e boot $(date) =="
md5sum /usr/bin/deniald
echo "proc1: $(cat /proc/1/comm 2>&1)"
} > /tmp/e2e.log 2>&1

cat > /root/.drirc <<'XML'
<driconf>
  <device driver="venus">
    <application name="all">
      <option name="venus_implicit_fencing" value="true"/>
    </application>
  </device>
</driconf>
XML
mkdir -p /tmp/.X11-unix; chmod 1777 /tmp/.X11-unix

# Forensics watcher: high-rate sampling during the boot/crash window (the
# guest-side shell dies ~3.5s in), then the slow 20s cadence. busybox wget
# saves the response body as the URL basename, so /tmp/snap must be removed
# before every POST or the follow-ups fail with "File exists".
( i=0
  while true; do
    if [ $i -lt 45 ]; then sleep 2; else sleep 20; fi
    i=$((i+1))
    rm -f /tmp/snap /root/snap
    {
      echo "== snap $i $(date) =="
      wc -c /tmp/dd.log 2>/dev/null
      wc -l /tmp/deniald-alive.log 2>/dev/null
      echo "-- all pids/comm --"
      for p in /proc/[0-9]*; do
        echo "  pid $(basename $p): $(cat $p/comm 2>/dev/null)"
      done
      DP=$(sed -n 's/^DENIAL_PID=//p' /tmp/e2e.log | head -1)
      if [ -n "$DP" ] && [ -d "/proc/$DP/task" ]; then
        echo "-- deniald threads (pid=$DP): $(ls /proc/$DP/task | wc -l)"
        for t in /proc/$DP/task/[0-9]*; do
          nm=$(cat $t/comm 2>/dev/null)
          st=$(grep -a "^State" $t/status 2>/dev/null | head -1)
          echo "  $nm | $st"
        done | head -30
      else
        echo "task dir missing"
      fi
      echo "-- e2e.log --"
      cat /tmp/e2e.log 2>/dev/null
      echo "-- deniald maps (dri/hostmem) --"
      if [ -n "$DP" ]; then
        grep -aE "card0|10000000|800000000|801000000" /proc/$DP/maps 2>/dev/null | head -20
      fi
      echo "-- exit.txt --"
      cat /tmp/exit.txt 2>/dev/null
      echo "-- audit tail --"
      sed 's/\x1b\[[0-9;]*m//g' /tmp/dd.log 2>/dev/null | grep -a "output scheduler audit" | tail -1 | grep -aoE "presentations=[0-9]+|missed_vblanks=[0-9]+|presentation_interval_p50_us=[0-9.]+|deadline_to_presentation_p50_us=[0-9.]+|ready_published=[0-9]+|render_requests=[0-9]+" | tr '\n' ' '
      echo ""
      echo "-- dd.log head 2500 --"
      head -c 2500 /tmp/dd.log 2>/dev/null
      echo ""
      echo "-- dd.log last 1200 --"
      tail -c 1200 /tmp/dd.log 2>/dev/null
      echo ""
    } > /tmp/snap.txt 2>&1
    wget -q --post-file=/tmp/snap.txt http://10.0.2.2:8000/snap
    # full dd.log for offline analysis; busybox wget saves the response body
    # as ./snap, so clean both known save paths before each POST
    rm -f /tmp/snap /root/snap
    wget -q --post-file=/tmp/dd.log http://10.0.2.2:8000/ddfull
  done ) &

/usr/bin/deniald --device /dev/dri/card0 --wayland --flutter-renderer skia \
  --flutter-bundle /opt/denial/shell-bundle >/tmp/dd.log 2>&1 &
DENIAL_PID=$!
echo "DENIAL_PID=$DENIAL_PID" >> /tmp/e2e.log
wait $DENIAL_PID
RC=$?
echo "DENIALD-EXITED rc=$RC at $(date)" >> /tmp/e2e.log
{
  echo "== deniald exited rc=$RC =="
  cat /tmp/e2e.log
  echo "-- alive log tail --"
  tail -3 /tmp/deniald-alive.log 2>/dev/null
  echo "-- dd.log tail --"
  tail -c 1200 /tmp/dd.log 2>/dev/null
} > /tmp/exit.txt 2>&1
wget -q --post-file=/tmp/exit.txt http://10.0.2.2:8000/denexit
exec /bin/sh
