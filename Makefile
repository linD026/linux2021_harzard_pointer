CFLAG=-D ANALYSIS_OPS

all:
	gcc -Wall -o list list.c -lpthread -g -fsanitize=thread
	
pure:
	gcc -Wall -o list list.c -lpthread -g

cnt:
	gcc $(CFLAG) -Wall -o list list.c -lpthread -g

ord:
	gcc $(CFLAG) -Wall -o ordered ordered.c -lpthread -g

rbtree:
	gcc -Wall -o rbtree vrb_listv1.c rbtree.c -lpthread -g
