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

outdir="."
dryrun=0

usage() {
    cat <<EOF
usage: $prog [<options>] <perf.data-file>

Scan the perf data file for MMAP records and copy the referenced files to the
output directory set via the -o option.

options:

  -h        this text
  -o <dir>  set the output directory to <dir> (default: $outdir)
  -n        print commands instead of executing them

<perf.data-file> defaults to perf.data.
EOF
}

while getopts "ho:n" opt; do
    case $opt in
        h)
            usage
            exit 0
            ;;
        o)
            outdir=$OPTARG
            ;;
        n)
            dryrun=1
            ;;
    esac
done

shift $(($OPTIND-1))


if [[ $# == 0 ]]; then
    file="perf.data"
elif [[ $# == 1 ]]; then
    file="$1"
    shift
fi

if [[ $# != 0 ]]; then
    echo "$prog: unknown argument: $1.  use -h for help."
    exit 1
fi

perf script --no-itrace -i "$file" -D | gawk -F' ' -- '
    function run(cmd) {
            if (dryrun != 0) {
                printf("%s\n", cmd)
            } else {
                system(cmd)
            }
    }

    function dirname(file) {
        items = split(file, parts, "/", seps)

        delete parts[items]

        dname = ""
        for (part in parts) {
            dname = dname seps[part-1] parts[part]
        }

        return dname
    }

    function handle_mmap(file) {
        # ignore any non-absolute filename
        #
        # this covers pseudo-files like [kallsyms] or [vdso]
        #
        if (substr(file, 0, 1) != "/") {
            return
        }

        # ignore kernel modules
        #
        # we rely on kcore
        #
        if (match(file, /\.ko$/) != 0) {
            return
        }

        # ignore //anon
        #
        if (file == "//anon") {
            return
        }

        dst = outdir file
        dir = dirname(dst)

        run("mkdir -p " dir)
        run("cp " file " " dst)
    }

    /PERF_RECORD_MMAP/     { handle_mmap($NF) }
' dryrun="$dryrun" outdir="$outdir"
