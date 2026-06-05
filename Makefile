NODE = node

# Build the native N-API addon into build/Release/ntpl_native.node.
# The `bindings` package locates it from there at runtime, so no copy step.
build:
	@echo "Building native addon..."
	@node-gyp rebuild

# Convenience wrapper that also pulls dependencies first.
deps:
	@echo "Installing dependencies..."
	@npm install

test:
	@echo "Testing..."
	@cd ./tests && $(NODE) run.js

clean:
	@echo "Cleaning build directory..."
	@node-gyp clean

all : clean build

dev : clean build test

.PHONY : build deps test clean all dev
