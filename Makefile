SCAF_INSTALL_DIR?=$(shell pwd)/install

CC= gcc
CXX= g++

all: scaf-debug

clean:
		rm -rf scaf-build-debug

uninstall:
		rm -rf $(SCAF_INSTALL_DIR)

scaf-debug:
	mkdir -p scaf-build-debug
	cd ./scaf-build-debug && \
	cmake -DCMAKE_INSTALL_PREFIX="$(SCAF_INSTALL_DIR)" -DCMAKE_BUILD_TYPE=Debug -DLLVM_ENABLE_UNWIND_TABLES=On -DCMAKE_CXX_FLAGS="-std=c++17" -DSVF_AVAILABLE=$(SVF_AVAILABLE) ../ && \
	make -j8 && \
	make install
