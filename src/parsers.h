#ifndef NINJA_PARSERS_H_
#define NINJA_PARSERS_H_

#include <string>
#include <vector>

using namespace std;

struct BindingEnv;

struct Token {
  enum Type {
    NONE,
    UNKNOWN,
    IDENT,
    RULE,
    BUILD,
    SUBNINJA,
    NEWLINE,
    EQUALS,
    COLON,
    PIPE,
    INDENT,
    OUTDENT,
    TEOF
  };
  explicit Token(Type type) : type_(type) {}

  void Clear() { type_ = NONE; }
  string AsString() const;

  Type type_;
  const char* pos_;
  const char* end_;
};

struct Tokenizer {
  Tokenizer()
      : token_(Token::NONE), line_number_(1),
        last_indent_(0), cur_indent_(-1) {}

  void Start(const char* start, const char* end);
  bool Error(const string& message, string* err);

  const Token& token() const { return token_; }

  void SkipWhitespace(bool newline=false);
  bool Newline(string* err);
  bool ExpectToken(Token::Type expected, string* err);
  bool ReadIdent(string* out);
  bool ReadToNewline(string* text, string* err);

  Token::Type PeekToken();
  void ConsumeToken();

  const char* cur_;
  const char* end_;

  const char* cur_line_;
  Token token_;
  int line_number_;
  int last_indent_, cur_indent_;
};

struct MakefileParser {
  bool Parse(const string& input, string* err);

  Tokenizer tokenizer_;
  string out_;
  vector<string> ins_;
};

struct State;

struct ManifestParser {
  struct FileReader {
    virtual bool ReadFile(const string& path, string* content, string* err) = 0;
  };

  ManifestParser(State* state, FileReader* file_reader);
  void set_root(const string& root) { root_ = root; }

  bool Load(const string& filename, string* err);
  bool Parse(const string& input, string* err);

  bool ParseRule(string* err);
  bool ParseLet(string* key, string* val, string* err);
  bool ParseEdge(string* err);
  bool ParseSubNinja(string* err);

  string ExpandFile(const string& file);

  State* state_;
  BindingEnv* env_;
  FileReader* file_reader_;
  Tokenizer tokenizer_;
  string builddir_;
  string root_;  // Absolute path to root ninja file.
};

#endif  // NINJA_PARSERS_H_
