LIB_PREFIX = $(HOME)/.node_libraries
NODE = node

build:
	@echo "Building..."
	@node-gyp clean && node-gyp configure && node-gyp build
	@echo "Copy relase file ntpl.native.node to lib"
	@cp build/Release/ntpl.native.node lib/nTPL/ntpl.native.node

install:
	@echo "Installing..."
	@mkdir -p $(LIB_PREFIX)
	@cp -fr lib/nTPL/* $(LIB_PREFIX)/

uninstall:
	@echo "Uninstalling ..."
	@rm -f $(LIB_PREFIX)/nTPL.js
	@rm -f $(LIB_PREFIX)/nTPL.native.node
	@rm -f $(LIB_PREFIX)/nTPL.block.js
	@rm -f $(LIB_PREFIX)/nTPL.filter.js

test:
	@echo "Testing..."
	@cd ./tests && $(NODE) run.js && cd ..

clean:
	@echo "Cleaning directory"
	@node-gyp clean
	@echo "Remove ntpl.native.node from lib"
	@rm lib/nTPL/ntpl.native.node

all : uninstall clean build install

dev : uninstall clean build install test

.PHONY : build install uninstall test
