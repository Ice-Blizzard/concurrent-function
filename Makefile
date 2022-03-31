all:
	g++ -pthread -o main main.cpp teams.cpp -lrt
	g++ -pthread -o new_process new_process.cpp -lrt
