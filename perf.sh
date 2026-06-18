#!/bin/bash
# 用法: ./perf.sh [客户端数] [每客户端请求数]

CLIENTS=${1:-50}
REQS=${2:-1000}

sudo /usr/share/zookeeper/bin/zkServer.sh start 2>/dev/null
sleep 1
pkill -f servers_main 2>/dev/null
sleep 1
./build/servers_main &>/tmp/server.log &
sleep 5

perf record -g -p $(pgrep servers_main) -o perf.data -- sleep 5 &
PERF_PID=$!
./build/benchmark $CLIENTS $REQS
wait $PERF_PID
perf script -i perf.data | ~/FlameGraph/stackcollapse-perf.pl | ~/FlameGraph/flamegraph.pl --colors=hot > flamegraph.svg
echo "火焰图: $(pwd)/flamegraph.svg"
