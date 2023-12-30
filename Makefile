FASTFLAGS = -O3	 			\
			-march=native

CFLAGS =	-std=c++20 						\
			-pedantic 						\
			-Wall 							\
			-Wno-deprecated-declarations	\
			${FASTFLAGS}

CXXFLAGS = ${CFLAGS}

LIBS = -lcurl

LDFLAGS = 	${LIBS} 		\
			-flto 			\
			-O3 			\
			-march=native 	\
			-std=c++20		


cc = clang
CXX = clang++

SRC = src/main.cpp

OBJ = $(SRC:../src/%.cpp=%.o)
DEPS = $(OBJ:.o=.d)

all: test

%.o: %.cpp
	${CXX} -c ${CXXFLAGS} $< -o $@ -MMD -MP


test: $(OBJ)
	${CXX} -o $@ $^ ${LDFLAGS}

clean:

backup:

depends:
	sudo chmod u+x install_depends.sh
	./install_depends.sh

dist: clean

install: all
	

uninstall:

.PHONY: all depends clean dist install uninstall
