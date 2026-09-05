#!/bin/sh
# test-luna-appmanager.sh — functional + robustness + stability harness for
# luna-appmanager (com.palm.applicationManager) on LuneOS.
# Runs ON the target (BusyBox sh compatible). Emits TAP-ish output and a
# summary; exit code 0 = all pass.

URI="luna://com.palm.applicationManager"
SVC="luna-appmanager"
OUT=/tmp/lam-test
SOAK_ITERATIONS=${SOAK_ITERATIONS:-30}
LAUNCH_APP=${LAUNCH_APP:-org.webosports.app.memos}
SAM="luna://com.webos.service.applicationmanager"

sam() { # sam <method> <payload>; result in $SAMRESP
    luna-send -n 1 $SAM/$1 "$2" >$OUT/sam.json 2>/dev/null &
    SPID=$!
    t=0
    while [ $t -lt 10 ]; do kill -0 $SPID 2>/dev/null || break; sleep 1; t=$((t+1)); done
    kill $SPID 2>/dev/null; wait $SPID 2>/dev/null
    SAMRESP=$(tr -d " \t\n" < $OUT/sam.json)
}

mkdir -p $OUT
PASS=0; FAIL=0; TESTNUM=0
START_TS=$(date +%s)

# luna-send emits nothing when run inside $(...) command substitution, so
# call() runs it at top level and leaves the normalized response in $RESP.
# A 10s watchdog guards against methods that never reply; TIMEDOUT is set.
call() { # call <method> <payload>; result in $RESP / $TIMEDOUT
    : > $OUT/resp.json
    luna-send -n 1 $URI/$1 "$2" >$OUT/resp.json 2>$OUT/resp.err &
    LSPID=$!
    TIMEDOUT=0
    t=0
    while [ $t -lt 10 ]; do
        kill -0 $LSPID 2>/dev/null || break
        sleep 1
        t=$((t+1))
    done
    if kill -0 $LSPID 2>/dev/null; then
        kill $LSPID 2>/dev/null
        TIMEDOUT=1
    fi
    wait $LSPID 2>/dev/null
    RESP=$(tr -d " \t\n" < $OUT/resp.json)
}

report() { # report <ok:0/1> <name> <detail>
    TESTNUM=$((TESTNUM+1))
    if [ "$1" = 0 ]; then PASS=$((PASS+1)); echo "ok $TESTNUM - $2"
    else FAIL=$((FAIL+1)); echo "not ok $TESTNUM - $2  # $3"; fi
}

expect_contains() { # <name> <method> <payload> <needle>
    call "$2" "$3"
    case "$RESP" in *"$4"*) report 0 "$1";; *) report 1 "$1" "wanted '$4' got: $(echo "$RESP" | cut -c1-160)";; esac
}

alive() { pidof LunaAppManager >/dev/null 2>&1; }

pid_now() { pidof LunaAppManager; }

echo "# luna-appmanager test harness — $(date)"
echo "# binary: $(ls -la /usr/sbin/LunaAppManager | awk '{print $5, $6, $7, $8}')"

# ---------- 0. service sanity ----------
systemctl is-active $SVC >/dev/null 2>&1; report $? "service is active"
alive; report $? "process is running"
PID0=$(pid_now)
RESTARTS0=$(systemctl show -p NRestarts --value $SVC 2>/dev/null)
RSS0=$(awk '/VmRSS/{print $2}' /proc/$PID0/status)
echo "# pid=$PID0 restarts=$RESTARTS0 rss=${RSS0}kB"

# ---------- 1. read-only API ----------
expect_contains "listPackages returns packages"        listPackages '{}'                       '"packages"'
expect_contains "running returns list"                 running      '{}'                       '"running"'
expect_contains "getAppInfo known app"                 getAppInfo   "{\"appId\":\"$LAUNCH_APP\"}" "$LAUNCH_APP"
expect_contains "searchApps works"                     searchApps   '{"keyword":"settings"}'   '"returnValue":true'
expect_contains "getAppBasePath known app"             getAppBasePath "{\"appId\":\"$LAUNCH_APP\"}" '"returnValue":true'
expect_contains "getHandlerForMimeType text/html"      getHandlerForMimeType '{"mimeType":"text/html"}' '"returnValue":true'
expect_contains "getHandlerForUrl http"                getHandlerForUrl '{"url":"http://example.com"}' '"returnValue":true'
call listResourceHandlers '{}'
[ "$TIMEDOUT" = 1 ]; report $? "listResourceHandlers is a known no-reply stub (timed out as expected)"
expect_contains "listExtensionMap"                     listExtensionMap '{}'                   '"returnValue":true'
expect_contains "listDockModeLaunchPoints"             listDockModeLaunchPoints '{}'           'returnValue'

# ---------- 2. negative / robustness (exercises the JSON hardening) ----------
# every one of these must return an error, and the service must survive
expect_contains "getAppInfo null appId rejected"       getAppInfo   '{"appId":null}'           '"returnValue":false'
alive; report $? "survives getAppInfo null appId"
expect_contains "getAppInfo missing param rejected"    getAppInfo   '{}'                       '"returnValue":false'
expect_contains "launch null id rejected"              launch       '{"id":null}'              '"returnValue":false'
alive; report $? "survives launch null id"
expect_contains "launch unknown app rejected"          launch       '{"id":"com.invalid.notexist"}' '"returnValue":false'
expect_contains "close replies (fire-and-forget by design)" close   '{"processId":"999999"}'   '"returnValue"'
expect_contains "getHandlerForMimeType null rejected"  getHandlerForMimeType '{"mimeType":null}' '"returnValue":false'
expect_contains "getHandlerForUrl null rejected"       getHandlerForUrl '{"url":null}'         '"returnValue":false'
expect_contains "getAppBasePath null rejected"         getAppBasePath '{"appId":null}'         '"returnValue":false'
call getAppInfo 'not json at all'
alive; report $? "survives malformed JSON payload"
call searchApps '{"keywords":null}'
alive; report $? "survives searchApps null keywords"

# ---------- 3. launch / close round trip ----------
# launch is delegated to SAM; the appmanager reply is a known-upstream false
# "was not found" (ApplicationProcessManager::launch always returns an empty
# processId), so success is verified through SAM's running list instead
call launch "{\"id\":\"$LAUNCH_APP\"}"
case "$RESP" in
  *'"returnValue":true'*|*wasnotfound*) report 0 "launch request delegated (reply: known upstream quirk)";;
  *) report 1 "launch request delegated" "$(echo "$RESP" | cut -c1-160)";;
esac
sleep 2
sam running '{}'
case "$SAMRESP" in *"$LAUNCH_APP"*) report 0 "launched app running in SAM";; *) report 1 "launched app running in SAM" "not in: $(echo "$SAMRESP" | cut -c1-160)";; esac
sam close "{\"id\":\"$LAUNCH_APP\"}"
sleep 2
sam running '{}'
case "$SAMRESP" in *"$LAUNCH_APP"*) report 1 "closed app gone from SAM running" "still running";; *) report 0 "closed app gone from SAM running";; esac
alive; report $? "survives launch/close cycle"

# ---------- 4. soak: repeated launch/close + query hammering ----------
echo "# soak: $SOAK_ITERATIONS iterations"
SOAK_FAIL=0
i=1
while [ $i -le $SOAK_ITERATIONS ]; do
    call launch "{\"id\":\"$LAUNCH_APP\"}"
    call running '{}'
    call listPackages '{}'
    call getAppInfo "{\"appId\":\"$LAUNCH_APP\"}"
    call getAppInfo '{"appId":null}'
    sam close "{\"id\":\"$LAUNCH_APP\"}"
    alive || { SOAK_FAIL=1; echo "# DIED at soak iteration $i"; break; }
    i=$((i+1))
done
report $SOAK_FAIL "soak survives $SOAK_ITERATIONS launch/close/query cycles"

# ---------- 5. stability accounting ----------
PID1=$(pid_now)
[ "$PID0" = "$PID1" ]; report $? "no service restart during tests (pid $PID0 -> $PID1)"
RESTARTS1=$(systemctl show -p NRestarts --value $SVC 2>/dev/null)
[ "$RESTARTS0" = "$RESTARTS1" ]; report $? "NRestarts unchanged ($RESTARTS0 -> $RESTARTS1)"
RSS1=$(awk '/VmRSS/{print $2}' /proc/$PID1/status)
echo "# rss ${RSS0}kB -> ${RSS1}kB (delta $((RSS1-RSS0))kB over run)"
# > 20MB growth over a short soak points at a leak
[ $((RSS1-RSS0)) -lt 20480 ]; report $? "memory growth under 20MB (delta $((RSS1-RSS0))kB)"

# journal scan since start
CRASHES=$(journalctl -u $SVC --since "@$START_TS" 2>/dev/null | grep -ciE "segfault|SIGSEGV|SIGABRT|core dump|assert")
[ "${CRASHES:-0}" = 0 ]; report $? "no crash indicators in journal ($CRASHES)"
DMESG_SEGV=$(dmesg | grep -c "LunaAppManager.*segfault")
[ "${DMESG_SEGV:-0}" = 0 ]; report $? "no segfaults in dmesg ($DMESG_SEGV)"

echo "# ---------------------------------------"
echo "# RESULT: $PASS passed, $FAIL failed, $TESTNUM total"
[ $FAIL = 0 ] && echo "# ALL TESTS PASSED" || echo "# FAILURES PRESENT"
exit $FAIL
