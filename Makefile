SRC:= main.cpp parser.cpp
PARAMS:= -std=c++11 -O3 -gdwarf-4 -lpthread -lstdc++
kab: main.cpp parser.cpp
	$(CC) $(SRC) -o $@ $(PARAMS)

clang: main.cpp parser.cpp
	clang -pedantic $(SRC) -o $@ $(PARAMS)

clean:
	-@rm kab clang
