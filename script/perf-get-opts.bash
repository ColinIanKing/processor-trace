#! /bin/bash
#
# Copyright (c) 2015-2016, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#  * Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#  * Neither the name of Intel Corporation nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

set -e

prog=`basename $0`

usage() {
    cat <<EOF
usage: $prog [<options>] <perf.data-file>

Create --pevent options for ptdump and ptxed based on <perf.data-file>
and previously generated <perf.data-file>-sideband*.pevent files.

When tracing ring-0, use perf-with-kcore and supply the path to kcore_dir
using the -k option.

options:
  -h         this text
  -m <file>  set <file> as the master sideband file
  -k <dir>   set the kcore directory to <dir>

<perf.data-file> defaults to perf.data.
EOF
}

master=""
kcore=""
while getopts "hm:k:" opt; do
    case $opt in
        h)
            usage
            exit 0
            ;;
        m)
            master=$OPTARG
            ;;
        k)
            kcore=$OPTARG
            ;;
    esac
done

shift $(($OPTIND-1))


if [[ $# == 0 ]]; then
    file="perf.data"
elif [[ $# == 1 ]]; then
    file="$1"
    shift
else
    usage
    exit 1
fi


pevent_opt() {
    file=$1
    opt=""

    if [[ $file == $master ]]; then
        opt="--pevent:primary $file"
    else
        opt="--pevent:secondary $file"
    fi

    echo " $opt"
}

global_sideband_opts=""
cpu_sideband_opts=""

if [[ -r $file-sideband.pevent ]]; then
  global_sideband_opts=$(pevent_opt $file-sideband.pevent)
fi

for sbfile in $(ls -1 $file-sideband-cpu*.pevent 2>/dev/null); do
    cpu_sideband_opts+=$(pevent_opt $sbfile)
done

auxtrace_info_opts=$(perf script --no-itrace -i $file -D | \
  grep -A14 -e PERF_RECORD_AUXTRACE_INFO | \
  gawk -F' ' -- '
  /^ *Time Shift/           { printf(" --pevent:time-shift %d", strtonum($3)) }
  /^ *Time Muliplier/       { printf(" --pevent:time-mult %d", strtonum($3)) }
  /^ *Time Multiplier/      { printf(" --pevent:time-mult %d", strtonum($3)) }
  /^ *Time Zero/            { printf(" --pevent:time-zero 0x%x", strtonum($3)) }
  /^ *TSC:CTC numerator/    { printf(" --cpuid-0x15.ebx %d", strtonum($3)) }
  /^ *TSC:CTC denominator/  { printf(" --cpuid-0x15.eax %d", strtonum($3)) }
')

attr_sample_types=$(perf evlist -v -i $file |  gawk -F' ' -- '
  BEGIN { RS = "," }
  /sample_type/  { print $2 }
' | sort | uniq)

gawk_sample_type() {
  echo $1 | gawk -- '
  BEGIN         { RS = "[|\n]" }
  /^TID$/        { config += 0x00002 }
  /^TIME$/       { config += 0x00004 }
  /^ID$/         { config += 0x00040 }
  /^CPU$/        { config += 0x00080 }
  /^STREAM$/     { config += 0x00200 }
  /^IDENTIFIER$/ { config += 0x10000 }
  END           {
    if (config != 0) {
      printf(" --pevent:sample-type 0x%x", config)
    }
  }
'
}

for attr in $attr_sample_types; do
    # We assume at most one attr with and at most one attr without CPU
    #
    if [[ $(echo $attr | grep -e CPU) ]]; then
        cpu_sample_type_opts=$(gawk_sample_type $attr)
    else
        global_sample_type_opts=$(gawk_sample_type $attr)
    fi
done

evlist_opts=$(perf evlist -v -i $file | grep intel_pt | gawk -F' ' -- '
  BEGIN             { RS = "," }
  /exclude_kernel/  { exclude_ring_0 = $2 }
  /exclude_user/    { exclude_ring_3 = $2 }
  /config/ {
    config = strtonum($2)
    mtc_freq = and(rshift(config, 14), 0xf)

    printf(" --mtc-freq 0x%x", mtc_freq)
  }
  END {
    if (exclude_ring_0 == 0) { printf(" --pevent:ring-0"); }
    if (exclude_ring_3 == 0) { printf(" --pevent:ring-3"); }
  }
')

cpu_opts=$(perf script --header-only -i $file | \
  gawk -F'[ ,]' -- '
    /^# cpuid : /  {
      vendor   = $4
      family   = strtonum($5)
      model    = strtonum($6)
      stepping = strtonum($7)

      if (vendor == "GenuineIntel") {
        printf(" --cpu %d/%d/%d", family, model, stepping)
      }
  }
')

if [[ ! -z $kcore ]]; then
    if [[ ! -d $kcore ]]; then
        echo "$prog: kcore_dir '$kcore' is not a directory."
        exit 1
    fi

    if [[ ! -r $kcore/kcore ]]; then
        echo "$prog: 'kcore' not found in '$kcore' or not readable."
        exit 1
    fi

    if [[ ! -r $kcore/kallsyms ]]; then
        echo "$prog: 'kallsyms' not found in '$kcore' or not readable."
        exit 1
    fi

    kernel_opts=$(cat $kcore/kallsyms | \
        gawk -M -- '
            function update_kernel_start(vaddr) {
              if (vaddr < kernel_start) {
                kernel_start = vaddr
              }
            }

            BEGIN                   { kernel_start = 0xffffffffffffffff }
            /^[0-9a-f]+ T _text$/   { update_kernel_start(strtonum("0x" $1)) }
            /^[0-9a-f]+ T _stext$/  { update_kernel_start(strtonum("0x" $1)) }
            END {
              if (kernel_start < 0xffffffffffffffff) {
                printf(" --pevent:kernel-start 0x%x", kernel_start)
              }
            }
        ')

    kernel_opts+=" --pevent:kcore $kcore/kcore"
fi

echo "$cpu_opts $auxtrace_info_opts $kernel_opts $evlist_opts \
  $cpu_sample_type_opts $cpu_sideband_opts \
  $global_sample_type_opts $global_sideband_opts"
