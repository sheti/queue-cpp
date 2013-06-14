all:
	rm queue
	g++ main.cpp -lyaml-cpp -lpthread -o queue
