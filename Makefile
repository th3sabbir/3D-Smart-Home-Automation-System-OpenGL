.PHONY: win build clean

win:
	g++.exe -fdiagnostics-color=always -I./include ./src/main.cpp -o ./build/main.exe -lfreeglut -lglu32 -lopengl32 -lgdi32
	./build/main.exe

build:
	g++.exe -fdiagnostics-color=always -I./include ./src/main.cpp -o ./build/main.exe -lfreeglut -lglu32 -lopengl32 -lgdi32

clean:
	rm -f ./build/main.exe

linux:
	g++ -fdiagnostics-color=always -I./include ./src/main.cpp ./src/glad.c -o ./build/main -Llib -lglfw -lGL -lXrandr -lX11 -lrt -ldl
	./build/main