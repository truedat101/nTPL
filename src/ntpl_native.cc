/*
 * nTPL native parser
 *
 * A modern N-API (node-addon-api) reimplementation of the original C++/V8
 * template parser. The public contract is unchanged:
 *
 *   parse(source, modificators, namespace, options)
 *     -> { replacements: [string], replVars: [int], code: [string] }
 *
 * The parser is a small state machine that walks the source byte-by-byte,
 * splitting it into plain-text chunks and {% modificator %} / {* comment *}
 * regions. Plain text becomes a "replacement" (referenced by index) plus a
 * "$p($N,$_);" instruction; modificator regions are handed to the matching
 * JS function/string in the `modificators` object, whose return value is
 * appended to `code`.
 *
 * Delimiters ({, }, %, *, space, tab) are all single-byte ASCII, so scanning
 * raw UTF-8 bytes and slicing on byte offsets is safe for multi-byte input —
 * a UTF-8 continuation byte never collides with an ASCII delimiter.
 *
 * Derived from nTPL.native v.0.3.0, Copyright 2010 Fedor Indutny, MIT license.
 */
#include <napi.h>
#include <string>

namespace {

enum ParserState {
  STAND_BY = 0,
  BRACES_MODIFICATOR = 1,
  BRACES = 2,
  COMMENT_BRACES = 3
};

// Append the plain-text slice source[last, current) as a replacement and emit
// the matching "$p($N,$_);" instruction into the code array. No-op for empty
// slices so adjacent tags don't create blank replacements.
inline void PushVariable(const Napi::Env& env, const std::string& source,
                         size_t last, size_t current,
                         const Napi::Array& replacements,
                         const Napi::Array& replVars, uint32_t& replCount,
                         const Napi::Array& code, uint32_t& codePos) {
  if (current <= last) return;

  replacements.Set(replCount,
                   Napi::String::New(env, source.data() + last, current - last));
  replVars.Set(replCount, Napi::Number::New(env, static_cast<double>(replCount)));
  code.Set(codePos++,
           Napi::String::New(env, "$p($" + std::to_string(replCount) + ",$_);"));
  replCount++;
}

// Resolve a {% modificator code %} region to its generated source.
//   - modificator maps to a function: call it as fn(code, namespace, options)
//   - modificator maps to a (defined) value: use that value verbatim
//   - modificator is unknown/undefined: emit the literal "<modificator><code>"
std::string CallModificator(const Napi::Env& env, const std::string& modificator,
                            const std::string& code,
                            const Napi::Object& modificators,
                            const Napi::Value& ns, const Napi::Value& options) {
  if (!modificator.empty() && modificators.Has(modificator)) {
    Napi::Value handler = modificators.Get(modificator);

    if (!handler.IsFunction()) {
      if (!handler.IsUndefined()) {
        return handler.ToString().Utf8Value();
      }
      return modificator + code;
    }

    Napi::Value result = handler.As<Napi::Function>().Call(
        modificators, {Napi::String::New(env, code), ns, options});
    return result.ToString().Utf8Value();
  }

  return modificator + code;
}

Napi::Value Parse(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  Napi::Object result = Napi::Object::New(env);
  Napi::Array replacements = Napi::Array::New(env);
  Napi::Array replVars = Napi::Array::New(env);
  Napi::Array code = Napi::Array::New(env);
  result.Set("replacements", replacements);
  result.Set("replVars", replVars);
  result.Set("code", code);

  // Expected: parse(string, object, object, object). On misuse return the
  // well-formed empty result so the JS layer degrades to a plain template
  // rather than crashing on undefined fields.
  if (info.Length() < 4 || !info[0].IsString() || !info[1].IsObject() ||
      !info[2].IsObject() || !info[3].IsObject()) {
    return result;
  }

  const std::string source = info[0].As<Napi::String>().Utf8Value();
  Napi::Object modificators = info[1].As<Napi::Object>();
  Napi::Value ns = info[2];
  Napi::Value options = info[3];

  const size_t len = source.size();
  size_t last = 0, current = 0;
  uint32_t replCount = 0, codePos = 0;
  std::string modificator;
  ParserState state = STAND_BY;

  // Both bytes of a two-char token must be in bounds.
  auto isTwo = [&](char a, char b) {
    return current + 1 < len && source[current] == a && source[current + 1] == b;
  };
  auto isSpace = [&]() {
    return source[current] == ' ' || source[current] == '\t';
  };

  while (current < len) {
    // Open code braces: {% ... %}
    if (state == STAND_BY && isTwo('{', '%')) {
      PushVariable(env, source, last, current, replacements, replVars,
                   replCount, code, codePos);
      state = BRACES_MODIFICATOR;
      current += 2;
      last = current;
    }
    // First whitespace ends the modificator name: {%name ... %}
    else if (state == BRACES_MODIFICATOR && isSpace()) {
      modificator = source.substr(last, current - last);
      state = BRACES;
      last = current;
    }
    // Close code braces
    else if ((state == BRACES || state == BRACES_MODIFICATOR) &&
             isTwo('%', '}')) {
      // Name-only form, e.g. {%else%} or {%/if%}: the name was never closed
      // by whitespace, so capture it here and leave an empty code body.
      if (state == BRACES_MODIFICATOR) {
        modificator = source.substr(last, current - last);
        last = current;
      }
      std::string piece = CallModificator(
          env, modificator, source.substr(last, current - last), modificators,
          ns, options);
      if (!piece.empty()) code.Set(codePos++, Napi::String::New(env, piece));
      state = STAND_BY;
      current += 2;
      last = current;
    }
    // Open comment braces: {* ... *}
    else if (state == STAND_BY && isTwo('{', '*')) {
      PushVariable(env, source, last, current, replacements, replVars,
                   replCount, code, codePos);
      state = COMMENT_BRACES;
      current += 2;
      last = current;
    }
    // Close comment braces
    else if (state == COMMENT_BRACES && isTwo('*', '}')) {
      state = STAND_BY;
      current += 2;
      last = current;
    } else {
      current++;
    }
  }

  // Trailing plain text (only if we ended outside a tag/comment).
  if (state == STAND_BY) {
    PushVariable(env, source, last, current, replacements, replVars, replCount,
                 code, codePos);
  }

  return result;
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("parse", Napi::Function::New(env, Parse, "parse"));
  return exports;
}

}  // namespace

NODE_API_MODULE(ntpl_native, Init)
