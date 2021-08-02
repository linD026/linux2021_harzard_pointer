gcc -Wall -o list list.c -lpthread -g
for i in {1..1000}; do echo -ne "$i ";  ./list;  done

#sudo perf stat --repeat 1000 -e cache-misses,cache-references,instructions,cycles ./list

#gcc -Wall -o vrb_list vrb_list.c -lpthread -g
#for i in {1..1000}; do echo -ne "$i ";  ./vrb_list;  done

#sudo perf stat --repeat 1000 -e cache-misses,cache-references,instructions,cycles ./vrb_list


#for i in {1..1000}; do echo -ne "$i ";  ./list;  done
# -fsanitize=thread
