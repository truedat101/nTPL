mkdir -p /tmp/ntpl && cd /tmp/ntpl && curl -# -L http://github.com/donnerjack13589/ntpl/tarball/master | tar xz --strip 1 && make && make install && echo "Installation complete"