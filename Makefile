ifeq ($(O),)
CFLAGS += -O3
else
CFLAGS += -O$(O)
endif

MAIN = sgg_app

COMPILE_TIME = $(shell date +"%Y-%m-%d %H:%M:%S")
GIT_REVISION = $(shell git show -s --pretty=format:%h)

CFLAGS += -DCOMPILE_TIME="\"$(COMPILE_TIME)"\"
CFLAGS += -DGIT_REVISION="\"$(GIT_REVISION)"\"
CFLAGS += -DELPP_THREAD_SAFE

CC = g++ -Wall -g -std=c++11 
INCLUDE = -I/usr/local/include/ 
LIBS =  -L /usr/local/lib/ -lpthread -ldl

CPP = *.cpp
OBJ = *.o

release:
	rm -f $(MAIN)
	$(CC) $(CFLAGS) -D _DEBUG_OFF_ -c $(CPP) ${CFLAG} $(INCLUDE)
	$(CC) $(CFLAGS) -D _DEBUG_OFF_ -o ${MAIN} $(OBJ) ${CLINKER} $(LIBS)
	rm -f $(OBJ)

debug:
	rm -f $(MAIN)
	$(CC) $(CFLAGS) -c $(CPP) ${CFLAG} $(INCLUDE)
	$(CC) $(CFLAGS) -o ${MAIN} $(OBJ) ${CLINKER} $(LIBS)
	rm -f $(OBJ)
	
tags :
	ctags -R *
	
clean:
	rm -f *.o
	rm -f $(MAIN)	
	
