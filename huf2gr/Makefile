ifeq ($(OS),Windows_NT)
LIBS += -static
else
LIBS += -lm
endif

huf2gr:
	clang -O3 -Wall -mavx2 -mbmi -mlzcnt -s huf2gr.c util.c -o huf2gr $(LIBS)
