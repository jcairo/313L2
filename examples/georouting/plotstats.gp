#
set terminal x11
set xlabel "XLABEL"
set ylabel "Message delivery ratio (%)"
set grid
set xrange [0:]
set yrange [0:]
#
plot "ratios" lw 2 with linespoints
pause -1
