ifeq ($(OS),Windows_NT)
LIBS += -static
else
LIBS += -lm
endif

huf2gr:
	gcc -O3 -Wall -mavx2 -mbmi -mlzcnt  huf2gr.c util.c  -o huf2gr $(LIBS)
