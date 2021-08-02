all:
	gcc -Wall -o list list.c -lpthread -g -fsanitize=thread
pure:
	gcc -Wall -o list list.c -lpthread -g 
