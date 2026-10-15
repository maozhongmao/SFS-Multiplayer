#!/bin/sh
# 日志轮转。必须原地截断，不能用 mv 顶替：
# 服务端/mefrpc 是以 >> 常驻打开这些文件的，mv 会把目录项换成新 inode，
# 老 fd 继续写已被 unlink 的旧文件，日志从此静默丢失（24 小时跑最坑的一点）。
for f in /opt/sfs/*.log /mnt/UDISK/sfs-archive/*.log; do
  [ -f "$f" ] || continue
  sz=$(wc -c < "$f" 2>/dev/null || echo 0)
  if [ "$sz" -gt 2097152 ]; then
    tail -n 200 "$f" > "$f.tmp" 2>/dev/null && cat "$f.tmp" > "$f" && rm -f "$f.tmp"
    echo "$(date '+%F %T') rotated $f was=$sz" >> /opt/sfs/init.log
  fi
done
