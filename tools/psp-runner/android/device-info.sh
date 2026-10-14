#!/system/bin/sh
#What a run on the device ran beside (run-on-device.sh runs it before and after each run, into the run's folder):
#the device and its build, the kernel's perf settings, the shell's cgroups, the CPUs' clocks and governors, the GPU's
#clock, the temperatures, the battery, the CPUs' PMUs. Only reads.
echo "time: $(date '+%Y-%m-%d %H:%M:%S %z')"
echo "uptime: $(cat /proc/uptime)"
echo "loadavg: $(cat /proc/loadavg)"
for name in ro.product.model ro.product.device ro.soc.manufacturer ro.soc.model ro.build.fingerprint \
  ro.build.version.release ro.build.version.sdk ro.build.type; do
  echo "$name: $(getprop "$name")"
done
echo "kernel: $(uname -a)"
echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid 2>&1)"
echo "security.perf_harden: $(getprop security.perf_harden)"
echo "selinux: $(getenforce 2>&1)"
echo "shell cgroup: $(tr '\n' ' ' < /proc/self/cgroup 2>&1)"
echo "shell cpuset: $(cat /proc/self/cpuset 2>&1)"
echo "cpus online: $(cat /sys/devices/system/cpu/online)"
for policy in /sys/devices/system/cpu/cpufreq/policy*; do
  [ -d "$policy" ] || continue
  echo "${policy##*/}: cpus $(cat "$policy/related_cpus") governor $(cat "$policy/scaling_governor")" \
    "cur $(cat "$policy/scaling_cur_freq") min $(cat "$policy/scaling_min_freq")" \
    "max $(cat "$policy/scaling_max_freq") top $(cat "$policy/cpuinfo_max_freq")"
done 2> /dev/null
for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
  echo "${cpu##*/}: online $(cat "$cpu/online" 2> /dev/null) capacity $(cat "$cpu/cpu_capacity" 2> /dev/null)" \
    "midr $(cat "$cpu/regs/identification/midr_el1" 2> /dev/null)"
done
for name in gpuclk max_gpuclk gpu_busy_percentage thermal_pwrlevel devfreq/cur_freq devfreq/governor; do
  [ -r "/sys/class/kgsl/kgsl-3d0/$name" ] && echo "kgsl $name: $(cat "/sys/class/kgsl/kgsl-3d0/$name" 2> /dev/null)"
done
for zone in /sys/class/thermal/thermal_zone*; do
  echo "thermal $(cat "$zone/type" 2> /dev/null): $(cat "$zone/temp" 2> /dev/null)"
done
dumpsys battery 2> /dev/null | grep -E 'level|temperature|status|powered'
for pmu in /sys/bus/event_source/devices/*; do
  echo "pmu ${pmu##*/}: type $(cat "$pmu/type" 2> /dev/null) cpus $(cat "$pmu/cpus" 2> /dev/null)"
done
