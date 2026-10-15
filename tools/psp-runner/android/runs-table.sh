#!/usr/bin/env bash
#A table of runs on a device (run-on-device.sh's folders), from each one's measure.json and report.json: the build,
#the renderer that drew, the speed, and the emulation thread's work per frame (CPU time, instructions, cycles), its
#IPC and clock, the CPU it ran on most, its slowest frames, the time a frame it waited for the GPU (the runner's gpu
#line: a Vulkan renderer's frames read back, unless --late-frames), and how the run ended. Needs jq.
#usage: runs-table.sh RUN_FOLDER...
set -euo pipefail
(($#)) || { echo "usage: $0 RUN_FOLDER..." >&2; exit 2; }
command -v jq > /dev/null || { echo "runs-table.sh needs jq" >&2; exit 1; }
{
  printf 'run\tsource\trenderer\tframes\tfps\tcpu ms/f\tcpu %%\tM instr/f\tM cycles/f\tIPC\tGHz\t'
  printf 'cpu (frames)\tMHz\tp99 ms\tgpu wait ms/f\texit\n'
  for folder in "$@"; do
    folder=${folder%/}
    code=$(cat "$folder/exit-code" 2> /dev/null || echo -)
    renderer=-
    if [[ -f $folder/report.json ]]; then
      renderer=$(jq -r '.ran.renderer // "-"' "$folder/report.json" | tr ' ' '_')
    elif [[ -f $folder/stdout.log ]]; then
      #(a runner without --report: the Vulkan renderer asked for where its gpu line is there, else the software one)
      renderer=Software
      if grep -q '^gpu: ' "$folder/stdout.log"; then
        case $(grep -o "'--renderer' '[^']*'" "$folder/run.sh" 2> /dev/null | tail -1) in
          *Vulkan-fast*) renderer='Vulkan_(fast)' ;;
          *) renderer=Vulkan ;;
        esac
      fi
    fi
    if grep -q "'--late-frames'" "$folder/run.sh" 2> /dev/null; then renderer+=,late; fi
    waiting=$(sed -n 's/^gpu: .*, \([0-9.]*\) ms a frame waiting.*/\1/p' "$folder/stdout.log" 2> /dev/null | tail -1)
    if [[ ! -f $folder/measure.json ]]; then
      printf '%s\t-\t%s\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t%s\t%s\n' "$(basename "$folder")" "$renderer" "${waiting:--}" \
        "$code"
      continue
    fi
    jq -r --arg run "$(basename "$folder")" --arg renderer "$renderer" --arg waiting "${waiting:--}" \
      --arg code "$code" '
      def div(a; b): if a == null or b == null or b == 0 then null else a / b end;
      def fixed(x; places): if x == null then "-" else (x * pow(10; places) | round) / pow(10; places) | tostring end;
      .window.frames as $frames
      | (if .counters.available then .counters.total.instructions else null end) as $instructions
      | (if .counters.available then .counters.total.cycles else null end) as $cycles
      | (."emulation thread"."cpu seconds") as $cpu
      | (."frames by cpu" | to_entries | map(select(.key != "-1")) | max_by(.value)) as $top
      | [$run, (.build.source // "-"), $renderer, ($frames | tostring), fixed(.window."frames a second"; 2),
         fixed(div($cpu; $frames) | if . == null then null else . * 1000 end; 3),
         fixed(div($cpu; .window.seconds) | if . == null then null else . * 100 end; 1),
         fixed(div($instructions; $frames) | if . == null then null else . / 1e6 end; 3),
         fixed(div($cycles; $frames) | if . == null then null else . / 1e6 end; 3),
         fixed(div($instructions; $cycles); 3),
         fixed(div($cycles; $cpu) | if . == null then null else . / 1e9 end; 3),
         (if $top == null then "-" else "cpu\($top.key) (\($top.value))" end),
         (if $top == null then "-" else fixed(div(.clocks."by cpu"[$top.key]."mean khz"; 1000); 0) end),
         fixed(."frame times"."p99 ms"; 2), $waiting, $code]
      | @tsv' "$folder/measure.json" || printf '%s\t(unreadable measure.json)\n' "$(basename "$folder")"
  done
} | column -t -s $'\t'
