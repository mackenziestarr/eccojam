# Makefile - Mac OS/ Linux
# Eccojam
#

CC = gcc
CFLAGS = -std=c99 -Wall -D__MACOSX_CORE__ 
LIBS= -lportaudio -lsndfile -lsamplerate -lncurses -framework OpenGL -framework GLUT
EXE = eccojam 


SRCS = eccojam.c
HDRS = 

$(EXE): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(LIBS) -o $@ $(SRCS) 

clean:
	rm -f *~ core $(EXE) *.o
	rm -rf $(EXE).dSYM 
