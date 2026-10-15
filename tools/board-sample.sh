#!/bin/sh
# SFS 服务端性能采样器：每次输出一行 CSV
# 用法: sh board-sample.sh [间隔秒] [采样次数(0=无限)]
# 输出列:
#   ts, mem_total, mem_used, mem_free, mem_buffcache, mem_avail,
#   load1, load5, load15,
#   rx_bytes, tx_bytes, rx_pkts, tx_pkts,
#   tcp_est, pid, vmrss_kb, vmsize_kb, utime, stime, threads

INTERVAL=${1:-2}
COUNT=${2:-0}
PROC=SFS-Multiplayer-Server
NETIF=eth1

i=0
while :; do
    TS=$(date +%s)

    PID=$(pidof $PROC 2>/dev/null | awk '{print $1}')
    if [ -z "$PID" ]; then
        PID=$(ps 2>/dev/null | grep "[S]FS-Multiplayer-Server" | awk '{print $1}' | head -1)
    fi

    MEM=$(free -k 2>/dev/null | awk '/^Mem:/{printf "%s,%s,%s,%s,%s", $2,$3,$4,$6,$7}')
    LOAD=$(cut -d' ' -f1-3 /proc/loadavg 2>/dev/null | tr ' ' ',')

    RB=$(cat /sys/class/net/$NETIF/statistics/rx_bytes   2>/dev/null || echo 0)
    TB=$(cat /sys/class/net/$NETIF/statistics/tx_bytes   2>/dev/null || echo 0)
    RP=$(cat /sys/class/net/$NETIF/statistics/rx_packets 2>/dev/null || echo 0)
    TP=$(cat /sys/class/net/$NETIF/statistics/tx_packets 2>/dev/null || echo 0)

    EST=$(awk '$4=="01"{n++} END{print n+0}' /proc/net/tcp 2>/dev/null)

    if [ -n "$PID" ] && [ -d "/proc/$PID" ]; then
        CPUSTAT=$(awk '{print $14","$15","$20}' /proc/$PID/stat 2>/dev/null)
        VMRSS=$(awk '/^VmRSS/{print $2}'  /proc/$PID/status 2>/dev/null)
        VMSIZE=$(awk '/^VmSize/{print $2}' /proc/$PID/status 2>/dev/null)
        [ -z "$VMRSS" ]  && VMRSS=0
        [ -z "$VMSIZE" ] && VMSIZE=0
        [ -z "$CPUSTAT" ] && CPUSTAT="0,0,0"
    else
        PID=0; VMRSS=0; VMSIZE=0; CPUSTAT="0,0,0"
    fi

    echo "$TS,$MEM,$LOAD,$RB,$TB,$RP,$TP,$EST,$PID,$VMRSS,$VMSIZE,$CPUSTAT"

    i=$((i + 1))
    if [ "$COUNT" != "0" ] && [ "$i" -ge "$COUNT" ]; then
        break
    fi
    sleep "$INTERVAL"
done
