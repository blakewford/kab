PARAMS:= -std=c++11 -O3 -gdwarf-4 -lpthread -lstdc++
kab: main.cpp
	$(CC) $< -o $@ $(PARAMS)

clang: main.cpp
	clang -pedantic $< -o $@ $(PARAMS)

clean:
	-@rm kab clang
