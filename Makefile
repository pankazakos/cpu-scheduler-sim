ARGS = 2 6 3 3 0.5 2
FLAGS = -lpthread -lrt -g3

compile:
	g++ simulator.cpp -o simulator $(FLAGS)

run:
	./simulator $(ARGS)

valgrind:
	valgrind ./simulator $(ARGS)

gdb:
	gdb ./simulator $(ARGS)

clean:
	rm -f *.o simulator
