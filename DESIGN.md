# nTPL design notes

This document describes how nTPL is put together and the rationale behind the
0.5.0 modernization. It is aimed at maintainers, not end users — for usage see
`README.rdoc`.

## Overview

nTPL turns a template string into a cached JavaScript render function:

```
source ──parse()──▶ { replacements, replVars, code } ──compile──▶ render(args) ──▶ string
   (native N-API)        (intermediate form)            (eval in nTPL.js)
```

1. **Parse (native).** `src/ntpl_native.cc` scans the source and splits it into
   plain-text chunks and `{% ... %}` / `{* ... *}` regions.
2. **Compile (JS).** `lib/nTPL/nTPL.js` assembles the parser output into a
   function body and `eval`s it once, producing a render function.
3. **Render.** The render function joins literal chunks with the output of the
   compiled code and returns the final string. Compiled functions are cached
   (by name, when given).

## Components

| File | Role |
|------|------|
| `src/ntpl_native.cc` | Native parser (N-API / node-addon-api). Exposes `parse()`. |
| `binding.gyp` | node-gyp build config for the addon. |
| `lib/nTPL/nTPL.native.js` | Loader; uses `bindings` to find the compiled `.node`. |
| `lib/nTPL/nTPL.js` | Compiler, caching, file watching, async loading, public API. |
| `lib/nTPL/nTPL.block.js` | `{%block%}` / `{%extends%}` template inheritance plugin. |
| `lib/nTPL/nTPL.filter.js` | `{%filter%}` plugin (escape / lower / upper / safe). |

## The parser (`src/ntpl_native.cc`)

A small state machine over the raw UTF-8 bytes:

```
STAND_BY ──"{%"──▶ BRACES_MODIFICATOR ──space/tab──▶ BRACES ──"%}"──▶ STAND_BY
STAND_BY ──"{*"──▶ COMMENT_BRACES ──"*}"──▶ STAND_BY
```

- Delimiters (`{ } % *`, space, tab) are single-byte ASCII, so byte-offset
  slicing is UTF-8-safe — a multi-byte continuation byte never collides with a
  delimiter. (Covered by the Cyrillic test, `tests/tests/test-26.js`.)
- Plain text becomes a **replacement** (stored by index in `replacements`, with
  its index pushed to `replVars`) plus a `$p($N,$_);` instruction in `code`.
- A `{% name body %}` region resolves `name` against the `modificators` object:
  - a **function** → called as `fn(body, namespace, options)`, its return value
    is emitted into `code`;
  - a **defined non-function value** → emitted verbatim (e.g. `'else': '}else{'`);
  - **unknown** → the literal `name + body` is emitted.

`parse(source, modificators, namespace, options)` returns
`{ replacements: string[], replVars: number[], code: string[] }`. On argument
misuse it returns a well-formed empty result so the JS layer degrades to a plain
(non-code) template instead of throwing.

### Why N-API (vs the old V8 code or pure JS)

- The original parser used V8 APIs removed around 2014 (`String::New`,
  `String::NewSymbol`, `const Arguments&`, `Handle`, `scope.Close`) and did not
  compile on modern Node.
- **N-API / node-addon-api** is ABI-stable: the compiled `.node` keeps working
  across Node major versions without recompiling against each V8.
- The rewrite uses `std::string` slicing instead of `sprintf` into a fixed
  `new char[28]` buffer and manual pointer arithmetic — removing the memory
  hazards of the original.
- A pure-JS parser is a viable future option (no toolchain needed to install);
  the `parse()` contract above is the seam where it could be swapped in.

## The compiler (`lib/nTPL/nTPL.js`)

`$main()` orchestrates: load source (sync or async, optionally watched), call
the native `parse()`, then build a function:

```js
(function($repl...) {
  return function($args, <declared args...>, $_) {
    $_ = [];
    function $p(a, $_) { $_[$_.length] = a }
    <joined code>;
    return $_.join("")
  }
}).apply(this, $rep)
```

- `$_` is the output stack; `$p` pushes onto it; the result is `$_.join("")`.
- Replacements are frozen and passed in as `$rep`, referenced as `$0`, `$1`, …
- A `namespace` object (`$scope`) persists per template for plugin state.
- If a template has no code (pure text), compilation is skipped and a function
  returning the literal string is used.

### Built-in modificators

`=` (output), `if` / `elseif` / `else` / `/if`, `each` / `/each`,
`catch` / `/catch`, and `set`. `set` (added in 0.5.0, implemented in JS) reads
`{%set key value%}`: the first whitespace-delimited token is the key (dotted
keys nest), the remainder is the value (comma-lists become arrays). It mutates
the `options` object, which `$main` reads back for `name` and `args`.

### Caching, watching, async

- Named templates are cached in `namecache`; `ntpl("name")` returns the cached
  render function.
- `watch: true` uses `fs.watchFile` to recompile on change and swaps the
  internal function in place (`refreshTemplate` / `replaceScope`).
- `callback: fn` loads the source asynchronously (`fs.access` + `fs.readFile`)
  and delivers the render function via the callback.
- A `process.on('exit')` handler unwatches all files.

## Public API

`require("nTPL")` exposes `ntpl` (a.k.a. `parse`, `nTPL` — all the same
function), `plugins()`, `render()` (Express-style `"name@file"`), and
`modificators`. Conventional entry point:

```js
var ntpl = require("nTPL").plugins("nTPL.block", "nTPL.filter").ntpl;
```

The exports object is frozen after setup.

## Security

Templates are compiled with `eval`, so a template source is executable code.
**Do not compile attacker-controlled template strings.** Rendering data
(`render(args)`) is safe; the template body itself is the trust boundary. The
`{%filter safe%}` / `{%filter escape%}` helpers escape *output values*, not the
template source.

## Building & testing

- `npm install` builds the addon (`node-gyp rebuild` via the `install` script).
- `npm run build` rebuilds after editing `src/ntpl_native.cc`.
- `npm test` runs `tests/run.js` (37 tests). The runner must execute with the
  repo root reachable; data-file tests use paths relative to the `tests/` dir.

## Known limitations / future work

- `eval`-based compilation (see Security). A sandboxed or pure-JS variant could
  be offered behind the same `parse()` seam.
- Requires a C/C++ toolchain to install (node-gyp). Prebuilt binaries
  (e.g. `prebuildify` / `node-gyp-build`) would remove that for consumers.
