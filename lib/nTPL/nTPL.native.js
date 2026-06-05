/*
 * Loader for the compiled N-API parser (src/ntpl_native.cc).
 *
 * `bindings` locates the built binary in the usual node-gyp output locations
 * (build/Release, build/Debug, ...), so this works whether the package was
 * installed from npm or built locally with `npm run build`.
 *
 * Exposes: { parse(source, modificators, namespace, options) }
 */
module.exports = require('bindings')('ntpl_native');
