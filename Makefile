#
# Makefile to build a TS3 plugin for linux
#

CC = g++
CFLAGS = -c -fPIC -std=c++11 -I./include
LD = $(CC)
LDFLAGS = -shared

TARGET = blinkbot
OBJECTS = $(patsubst %.cpp, %.o, $(wildcard *.cpp))

all: clean $(TARGET)

$(TARGET): $(OBJECTS)
	$(LD) -o ./out/lib$(TARGET).so $^ $(LDFLAGS)
	#cp ./out/lib$(TARGET).so /home/tsbot/musicbot/TeamSpeak3-Client-linux_amd64/plugins

%.o: %.cpp
	$(CC) $^ $(CFLAGS)

clean:
	rm -rf *.o
