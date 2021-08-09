YES="Y"

read -p "analysis [Y/N] " ANAL
read -p "perf     [Y/N] " PERF

if [ $ANAL == $YES ]
then
    CFLAG='-D ANALYSIS_OPS'
else
    CFLAG=''
fi

if [ $PERF == $YES ]
then 
    PERF_COM='sudo perf stat --repeat 1000 -e cache-misses,cache-references,instructions,cycles'
else
    PERF_COM=''
fi

#########################

echo "## default list"
gcc ${CFLAG} -Wall -o list list.c -lpthread -g
$PERF_COM ./list
echo ""

##########################

echo "## ordered list v1"
gcc $CFLAG -Wall -o ordered ordered.c -lpthread -g
$PERF_COM ./ordered
echo ""

##########################

echo "## ordered list v2"
gcc $CFLAG -Wall -o orderedv2 orderedv2.c -lpthread -g
$PERF_COM ./orderedv2
echo ""


rm -f list ordered orderedv2


#gcc -Wall -o vrb_list vrb_list.c -lpthread -g
#for i in {1..10}; do echo -ne "$i ";  ./vrb_list;  done

#for i in {1..1000}; do echo -ne "$i ";  ./list;  done
# -fsanitize=thread
