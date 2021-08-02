reset
set title "array and red black tree"
set xlabel "1000 times"
set ylabel "nanosecond"
#set format y '%l'
#set ytics 0, 1, 10
#set yrange [1000000:2000000]
#set yrange [500000:20000000]
#set ytics 100000, 10000, 5000000
set terminal png font "benchmark"
set output "ds_box.png"
set key left


set style fill solid 0.25 border -1
set style boxplot outliers pointtype 7
set style data boxplot
set xtics ('array' 1, 'rbtree' 2, 'rbtree v2' 3)
plot\
"array.txt" using (1):2  notitle,\
"rbtree.txt"  using (2):2  notitle,\
"rbtreev2.txt"  using (3):2  notitle,\



#plot\
#"array.txt"  using 1:2 with lines title "array",\
#"rbtree.txt" using 1:2 with lines title "rbtree",\
