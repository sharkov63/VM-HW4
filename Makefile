CC=gcc
CXX=g++
COMMON_FLAGS=-m32 -g2 -fstack-protector-all -O2
INTERPRETER_FLAGS=$(COMMON_FLAGS) -Ifmt/include -DFMT_HEADER_ONLY 
#REGRESSION_TESTS=$(sort $(filter-out test111, $(notdir $(basename $(wildcard Lama/regression/test*.lama)))))
LAMAC=lamac
RAPIDLAMA=$(realpath ./rapidlama)

all: rapidlama

runtime:
	$(MAKE) -C runtime

Main.o: Main.cpp ByteFile.h Interpreter.h
	$(CXX) -o $@ $(INTERPRETER_FLAGS) -c Main.cpp

GlobalArea.o: GlobalArea.s
	$(CXX) -o $@ $(INTERPRETER_FLAGS) -c GlobalArea.s

ByteFile.o: ByteFile.cpp ByteFile.h
	$(CXX) -o $@ $(INTERPRETER_FLAGS) -c ByteFile.cpp

Interpreter.o: Interpreter.cpp Interpreter.h
	$(CXX) -o $@ $(INTERPRETER_FLAGS) -c Interpreter.cpp

Barray_.o: Barray_.s
	$(CC) -o $@ $(INTERPRETER_FLAGS) -c Barray_.s

Bsexp_.o: Bsexp_.s
	$(CC) -o $@ $(INTERPRETER_FLAGS) -c Bsexp_.s

Bclosure_.o: Bclosure_.s
	$(CC) -o $@ $(INTERPRETER_FLAGS) -c Bclosure_.s

rapidlama: Main.o GlobalArea.o ByteFile.o Interpreter.o Barray_.o Bsexp_.o Bclosure_.o runtime
	$(CXX) -o $@ $(INTERPRETER_FLAGS) runtime/runtime.o runtime/gc.o Main.o GlobalArea.o ByteFile.o Interpreter.o Barray_.o Bsexp_.o Bclosure_.o

clean:
	$(RM) *.a *.o *~ rapidlama
	$(MAKE) clean -C runtime
	$(MAKE) clean -C regression
	$(MAKE) clean -C performance

regression: rapidlama
	$(MAKE) clean check -j8 -C regression

regression-expressions: rapidlama
	$(MAKE) clean check -j8 -C regression/expressions
	$(MAKE) clean check -j8 -C regression/deep-expressions

performance: rapidlama
	$(MAKE) clean check -C performance

.PHONY: all clean runtime regression regression-expressions performance
