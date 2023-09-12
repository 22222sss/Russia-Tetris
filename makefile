object = Tetris.o UserInfo.o Server.o main.o

main: $(object)
	g++ -o main $(object)

%.o: %.c
	g++ -c $<

clean:
	rm *.o
	rm main