SRC=$(dirname "$(readlink -f "$0")")/
#PRE=$HOME/buildroot/host/bin/x86_64-buildroot-linux-gnu-
OPT="-O2 -fno-plt"
EOPT=-fno-pie
STD="-std=c11"

# PRE=musl-
LDOPT="-Wl,--enable-new-dtags,-z,relro,-z,now -Wl,-as-needed"
#OPT="-O2"
LDOPT=""
# STD="-ansi"
#LDATOMIC=-latomic

${PRE}gcc -g2 $OPT $STD -Wall -Wextra -pedantic -fPIC   ${SRC}src/pchecker_gettime.c -ldl $LDATOMIC -shared -o libpchecker_gettime.so $LDOPT
${PRE}gcc -g2 $OPT $STD -Wall -Wextra -pedantic -fPIC   ${SRC}src/pchecker_heap.c  -ldl $LDATOMIC -shared -o libpchecker_heap.so $LDOPT
${PRE}gcc -g2 $OPT $STD -Wall -Wextra -pedantic -fPIC   ${SRC}src/pchecker_heap_glibc.c  -ldl $LDATOMIC -shared -o libpchecker_heap-glibc.so $LDOPT
${PRE}gcc -g2 $OPT $STD -Wall -Wextra -pedantic -fPIC   ${SRC}src/pchecker_heap_musl.c  -ldl $LDATOMIC -shared -o libpchecker_heap-musl.so $LDOPT

${PRE}gcc -g2 $OPT $STD -Wall -Wextra -pedantic -fPIC   ${SRC}test/pchecker_wrapper.c -shared -o libtestpchecker_wrapper.so $LDOPT
${PRE}gcc -g2 $OPT $STD -Wall -Wextra -pedantic $EOPT ${SRC}test/testpchecker.c -no-pie -L. -ltestpchecker_wrapper -o testpchecker $LDOPT
