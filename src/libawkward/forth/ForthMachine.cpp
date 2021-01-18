// BSD 3-Clause License; see https://github.com/scikit-hep/awkward-1.0/blob/main/LICENSE

#define FILENAME(line) FILENAME_FOR_EXCEPTIONS("src/libawkward/forth/ForthMachine.cpp", line)

#include <sstream>
#include <stdexcept>
#include <chrono>

#include "awkward/forth/ForthMachine.h"

namespace awkward {
  // Instruction values are preprocessor macros to be equally usable in 32-bit and
  // 64-bit instruction sets.

  // parser flags (parsers are combined bitwise and then bit-inverted to be negative)
  #define READ_DIRECT 1
  #define READ_REPEATED 2
  #define READ_BIGENDIAN 4
  // parser sequential values (starting in the fourth bit)
  #define READ_MASK (~(-0x80) & (-0x8))
  #define READ_BOOL (0x8 * 1)
  #define READ_INT8 (0x8 * 2)
  #define READ_INT16 (0x8 * 3)
  #define READ_INT32 (0x8 * 4)
  #define READ_INT64 (0x8 * 5)
  #define READ_INTP (0x8 * 6)
  #define READ_UINT8 (0x8 * 7)
  #define READ_UINT16 (0x8 * 8)
  #define READ_UINT32 (0x8 * 9)
  #define READ_UINT64 (0x8 * 10)
  #define READ_UINTP (0x8 * 11)
  #define READ_FLOAT32 (0x8 * 12)
  #define READ_FLOAT64 (0x8 * 13)

  // instructions from special parsing rules
  #define CODE_LITERAL 0
  #define CODE_HALT 1
  #define CODE_PAUSE 2
  #define CODE_IF 3
  #define CODE_IF_ELSE 4
  #define CODE_DO 5
  #define CODE_DO_STEP 6
  #define CODE_AGAIN 7
  #define CODE_UNTIL 8
  #define CODE_WHILE 9
  #define CODE_EXIT 10
  #define CODE_PUT 11
  #define CODE_INC 12
  #define CODE_GET 13
  #define CODE_LEN_INPUT 14
  #define CODE_POS 15
  #define CODE_END 16
  #define CODE_SEEK 17
  #define CODE_SKIP 18
  #define CODE_WRITE 19
  #define CODE_LEN_OUTPUT 20
  #define CODE_REWIND 21
  // generic builtin instructions
  #define CODE_I 22
  #define CODE_J 23
  #define CODE_K 24
  #define CODE_DUP 25
  #define CODE_DROP 26
  #define CODE_SWAP 27
  #define CODE_OVER 28
  #define CODE_ROT 29
  #define CODE_NIP 30
  #define CODE_TUCK 31
  #define CODE_ADD 32
  #define CODE_SUB 33
  #define CODE_MUL 34
  #define CODE_DIV 35
  #define CODE_MOD 36
  #define CODE_DIVMOD 37
  #define CODE_NEGATE 38
  #define CODE_ADD1 39
  #define CODE_SUB1 40
  #define CODE_ABS 41
  #define CODE_MIN 42
  #define CODE_MAX 43
  #define CODE_EQ 44
  #define CODE_NE 45
  #define CODE_GT 46
  #define CODE_GE 47
  #define CODE_LT 48
  #define CODE_LE 49
  #define CODE_EQ0 50
  #define CODE_INVERT 51
  #define CODE_AND 52
  #define CODE_OR 53
  #define CODE_XOR 54
  #define CODE_LSHIFT 55
  #define CODE_RSHIFT 56
  #define CODE_FALSE 57
  #define CODE_TRUE 58
  // beginning of the user-defined dictionary
  #define BOUND_DICTIONARY 59

  const std::set<std::string> reserved_words_({
    // comments
    "(", ")", "\\", "\n", "",
    // defining functinos
    ":", ";", "recurse",
    // declaring globals
    "variable", "input", "output",
    // manipulate control flow externally
    "halt", "pause",
    // conditionals
    "if", "then", "else",
    // loops
    "do", "loop", "+loop",
    "begin", "again", "until", "while", "repeat",
    // nonlocal exits
    "exit",
    // variable access
    "!", "+!", "@",
    // input actions
    "len", "pos", "end", "seek", "skip",
    // output actions
    "<-", "stack", "rewind"
  });

  const std::set<std::string> input_parser_words_({
    // single little-endian
    "?->", "b->", "h->", "i->", "q->", "n->", "B->", "H->", "I->", "Q->", "N->", "f->", "d->",
    // single big-endian
    "!h->", "!i->", "!q->", "!n->", "!H->", "!I->", "!Q->", "!N->", "!f->", "!d->",
    // multiple little-endian
    "#?->", "#b->", "#h->", "#i->", "#q->", "#n->", "#B->", "#H->", "#I->", "#Q->", "#N->", "#f->", "#d->",
    // multiple big-endian
    "#!h->", "#!i->", "#!q->", "#!n->", "#!H->", "#!I->", "#!Q->", "#!N->", "#!f->", "#!d->"
  });

  const std::map<std::string, util::dtype> output_dtype_words_({
    {"bool", util::dtype::boolean},
    {"int8", util::dtype::int8},
    {"int16", util::dtype::int16},
    {"int32", util::dtype::int32},
    {"int64", util::dtype::int64},
    {"uint8", util::dtype::uint8},
    {"uint16", util::dtype::uint16},
    {"uint32", util::dtype::uint32},
    {"uint64", util::dtype::uint64},
    {"float32", util::dtype::float32},
    {"float64", util::dtype::float64}
  });

  const std::map<std::string, int64_t> generic_builtin_words_({
    // loop variables
    {"i", CODE_I},
    {"j", CODE_J},
    {"k", CODE_K},
    // stack operations
    {"dup", CODE_DUP},
    {"drop", CODE_DROP},
    {"swap", CODE_SWAP},
    {"over", CODE_OVER},
    {"rot", CODE_ROT},
    {"nip", CODE_NIP},
    {"tuck", CODE_TUCK},
    // basic mathematical functions
    {"+", CODE_ADD},
    {"-", CODE_SUB},
    {"*", CODE_MUL},
    {"/", CODE_DIV},
    {"mod", CODE_MOD},
    {"/mod", CODE_DIVMOD},
    {"negate", CODE_NEGATE},
    {"1+", CODE_ADD1},
    {"1-", CODE_SUB1},
    {"abs", CODE_ABS},
    {"min", CODE_MIN},
    {"max", CODE_MAX},
    // comparisons
    {"=", CODE_EQ},
    {"<>", CODE_NE},
    {">", CODE_GT},
    {">=", CODE_GE},
    {"<", CODE_LT},
    {"<=", CODE_LE},
    {"0=", CODE_EQ0},
    // bitwise operations
    {"invert", CODE_INVERT},
    {"and", CODE_AND},
    {"or", CODE_OR},
    {"xor", CODE_XOR},
    {"lshift", CODE_LSHIFT},
    {"rshift", CODE_RSHIFT},
    // constants
    {"false", CODE_FALSE},
    {"true", CODE_TRUE}
  });

  template <typename T, typename I>
  ForthMachineOf<T, I>::ForthMachineOf(const std::string& source,
                                       int64_t stack_max_depth,
                                       int64_t recursion_max_depth,
                                       int64_t output_initial_size,
                                       double output_resize_factor)
    : source_(source)
    , output_initial_size_(output_initial_size)
    , output_resize_factor_(output_resize_factor)

    , stack_buffer_(new T[stack_max_depth])
    , stack_depth_(0)
    , stack_max_depth_(stack_max_depth)

    , current_inputs_()
    , current_outputs_()
    , is_ready_(false)

    , current_which_(new int64_t[recursion_max_depth])
    , current_where_(new int64_t[recursion_max_depth])
    , recursion_current_depth_(0)
    , recursion_max_depth_(recursion_max_depth)

    , do_recursion_depth_(new int64_t[recursion_max_depth])
    , do_stop_(new int64_t[recursion_max_depth])
    , do_i_(new int64_t[recursion_max_depth])
    , do_current_depth_(0)

    , current_error_(util::ForthError::none)

    , count_instructions_(0)
    , count_reads_(0)
    , count_writes_(0)
    , count_nanoseconds_(0)
  {
    std::vector<std::string> tokenized;
    std::vector<std::pair<int64_t, int64_t>> linecol;
    tokenize(tokenized, linecol);
    compile(tokenized, linecol);
  }

  template <typename T, typename I>
  ForthMachineOf<T, I>::~ForthMachineOf() {
    delete [] stack_buffer_;
    delete [] current_which_;
    delete [] current_where_;
    delete [] do_recursion_depth_;
    delete [] do_stop_;
    delete [] do_i_;
  }

  template <typename T, typename I>
  const std::string
  ForthMachineOf<T, I>::source() const noexcept {
    return source_;
  }

  template <typename T, typename I>
  const ContentPtr
  ForthMachineOf<T, I>::bytecodes() const {
    IndexOf<I> content((int64_t)bytecodes_.size(), kernel::lib::cpu);
    std::memcpy(content.data(), bytecodes_.data(), bytecodes_.size() * sizeof(I));

    IndexOf<int64_t> offsets((int64_t)bytecodes_offsets_.size(), kernel::lib::cpu);
    std::memcpy(offsets.data(), bytecodes_offsets_.data(), bytecodes_offsets_.size() * sizeof(int64_t));

    return std::make_shared<ListOffsetArrayOf<int64_t>>(Identities::none(),
                                                        util::Parameters(),
                                                        offsets,
                                                        std::make_shared<NumpyArray>(content),
                                                        false);
  }

  template <typename T, typename I>
  const std::string
  ForthMachineOf<T, I>::decompiled() const {
    bool first = true;
    std::stringstream out;

    for (auto const& name : variable_names_) {
      first = false;
      out << "variable " << name << std::endl;
    }

    for (auto const& name : input_names_) {
      first = false;
      out << "input " << name << std::endl;
    }

    for (IndexTypeOf<int64_t> i = 0;  i < output_names_.size();  i++) {
      first = false;
      out << "output " << output_names_[i] << " "
          << util::dtype_to_name(output_dtypes_[i]) << std::endl;
    }

    for (IndexTypeOf<int64_t> i = 0;  i < dictionary_names_.size();  i++) {
      if (!first) {
        out << std::endl;
      }
      first = false;
      int64_t segment_position = dictionary_bytecodes_[i] - BOUND_DICTIONARY;
      out << ": " << dictionary_names_[i] << std::endl
          << (segment_nonempty(segment_position) ? "  " : "")
          << decompiled_segment(segment_position, "  ")
          << ";" << std::endl;
    }

    if (!first  &&  bytecodes_offsets_[1] != 0) {
      out << std::endl;
    }
    out << decompiled_segment(0);
    return out.str();
  }

  template <typename T, typename I>
  const std::string
  ForthMachineOf<T, I>::decompiled_segment(int64_t segment_position,
                                           const std::string& indent) const {
    if ((IndexTypeOf<int64_t>)segment_position < 0  ||  (IndexTypeOf<int64_t>)segment_position + 1 >= bytecodes_offsets_.size()) {
      throw std::runtime_error(
        std::string("segment ") + std::to_string(segment_position)
        + std::string(" does not exist in the bytecode") + FILENAME(__LINE__));
    }
    std::stringstream out;
    int64_t bytecode_position = bytecodes_offsets_[(IndexTypeOf<int64_t>)segment_position];
    // FIXME: unused variable
    // int64_t instruction_number = 0;
    while (bytecode_position < bytecodes_offsets_[(IndexTypeOf<int64_t>)segment_position + 1]) {
      if (bytecode_position != bytecodes_offsets_[(IndexTypeOf<int64_t>)segment_position]) {
        out << indent;
      }
      out << decompiled_at(bytecode_position, indent) << std::endl;
      bytecode_position += bytecodes_per_instruction(bytecode_position);
    }
    return out.str();
  }

  template <typename T, typename I>
  const std::string
  ForthMachineOf<T, I>::decompiled_at(int64_t bytecode_position,
                                      const std::string& indent) const {
    if (bytecode_position < 0  ||  (IndexTypeOf<int64_t>)bytecode_position >= bytecodes_.size()) {
      throw std::runtime_error(
        std::string("absolute position ") + std::to_string(bytecode_position)
        + std::string(" does not exist in the bytecode") + FILENAME(__LINE__));
    }

    I bytecode = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position];
    I next_bytecode = 0;
    if ((IndexTypeOf<int64_t>)bytecode_position + 1 < bytecodes_.size()) {
      next_bytecode = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
    }

    if (bytecode < 0) {
      I in_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
      std::string in_name = input_names_[(IndexTypeOf<int64_t>)in_num];

      std::string rep = (~bytecode & READ_REPEATED) ? "#" : "";
      std::string big = ((~bytecode & READ_BIGENDIAN) != 0) ? "!" : "";
      std::string rest;
      switch (~bytecode & READ_MASK) {
        case READ_BOOL:
          rest = "?->";
          break;
        case READ_INT8:
          rest = "b->";
          break;
        case READ_INT16:
          rest = "h->";
          break;
        case READ_INT32:
          rest = "i->";
          break;
        case READ_INT64:
          rest = "q->";
          break;
        case READ_INTP:
          rest = "n->";
          break;
        case READ_UINT8:
          rest = "B->";
          break;
        case READ_UINT16:
          rest = "H->";
          break;
        case READ_UINT32:
          rest = "I->";
          break;
        case READ_UINT64:
          rest = "Q->";
          break;
        case READ_UINTP:
          rest = "N->";
          break;
        case READ_FLOAT32:
          rest = "f->";
          break;
        case READ_FLOAT64:
          rest = "d->";
          break;
      }
      std::string arrow = rep + big + rest;

      std::string out_name = "stack";
      if (~bytecode & READ_DIRECT) {
        I out_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 2];
        out_name = output_names_[(IndexTypeOf<int64_t>)out_num];
      }
      return in_name + std::string(" ") + arrow + std::string(" ") + out_name;
    }

    else if (next_bytecode == CODE_AGAIN) {
      int64_t body = bytecode - BOUND_DICTIONARY;
      return std::string("begin\n")
             + (segment_nonempty(body) ? indent + "  " : "")
             + decompiled_segment(body, indent + "  ")
             + indent + "again";
    }

    else if (next_bytecode == CODE_UNTIL) {
      int64_t body = bytecode - BOUND_DICTIONARY;
      return std::string("begin\n")
             + (segment_nonempty(body) ? indent + "  " : "")
             + decompiled_segment(body, indent + "  ")
             + indent + "until";
    }

    else if (next_bytecode == CODE_WHILE) {
      int64_t precondition = bytecode - BOUND_DICTIONARY;
      int64_t postcondition = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 2] - BOUND_DICTIONARY;
      return std::string("begin\n")
             + (segment_nonempty(precondition) ? indent + "  " : "")
             + decompiled_segment(precondition, indent + "  ")
             + indent + "while\n"
             + (segment_nonempty(postcondition) ? indent + "  " : "")
             + decompiled_segment(postcondition, indent + "  ")
             + indent + "repeat";
    }

    else if (bytecode >= BOUND_DICTIONARY) {
      for (IndexTypeOf<int64_t> i = 0;  i < dictionary_names_.size();  i++) {
        if (dictionary_bytecodes_[i] == bytecode) {
          return dictionary_names_[i];
        }
      }
      return "(anonymous segment at " + std::to_string(bytecode - BOUND_DICTIONARY) + ")";
    }

    else {
      switch (bytecode) {
        case CODE_LITERAL: {
          return std::to_string(bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1]);
        }
        case CODE_HALT: {
          return "halt";
        }
        case CODE_PAUSE: {
          return "pause";
        }
        case CODE_IF: {
          int64_t consequent = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1] - BOUND_DICTIONARY;
          return std::string("if\n")
                 + (segment_nonempty(consequent) ? indent + "  " : "")
                 + decompiled_segment(consequent, indent + "  ")
                 + indent + "then";
        }
        case CODE_IF_ELSE: {
          int64_t consequent = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1] - BOUND_DICTIONARY;
          int64_t alternate = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 2] - BOUND_DICTIONARY;
          return std::string("if\n")
                 + (segment_nonempty(consequent) ? indent + "  " : "")
                 + decompiled_segment(consequent, indent + "  ")
                 + indent + "else\n"
                 + (segment_nonempty(alternate) ? indent + "  " : "")
                 + decompiled_segment(alternate, indent + "  ")
                 + indent + "then";
        }
        case CODE_DO: {
          int64_t body = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1] - BOUND_DICTIONARY;
          return std::string("do\n")
                 + (segment_nonempty(body) ? indent + "  " : "")
                 + decompiled_segment(body, indent + "  ")
                 + indent + "loop";
        }
        case CODE_DO_STEP: {
          int64_t body = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1] - BOUND_DICTIONARY;
          return std::string("do\n")
                 + (segment_nonempty(body) ? indent + "  " : "")
                 + decompiled_segment(body, indent + "  ")
                 + indent + "+loop";
        }
        case CODE_EXIT: {
          return std::string("exit");
        }
        case CODE_PUT: {
          int64_t var_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
          return variable_names_[(IndexTypeOf<int64_t>)var_num] + " !";
        }
        case CODE_INC: {
          int64_t var_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
          return variable_names_[(IndexTypeOf<int64_t>)var_num] + " +!";
        }
        case CODE_GET: {
          int64_t var_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
          return variable_names_[(IndexTypeOf<int64_t>)var_num] + " @";
        }
        case CODE_LEN_INPUT: {
          int64_t in_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
          return input_names_[(IndexTypeOf<int64_t>)in_num] + " len";
        }
        case CODE_POS: {
          int64_t in_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
          return input_names_[(IndexTypeOf<int64_t>)in_num] + " pos";
        }
        case CODE_END: {
          int64_t in_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
          return input_names_[(IndexTypeOf<int64_t>)in_num] + " end";
        }
        case CODE_SEEK: {
          int64_t in_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
          return input_names_[(IndexTypeOf<int64_t>)in_num] + " seek";
        }
        case CODE_SKIP: {
          int64_t in_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
          return input_names_[(IndexTypeOf<int64_t>)in_num] + " skip";
        }
        case CODE_WRITE: {
          int64_t out_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
          return output_names_[(IndexTypeOf<int64_t>)out_num] + " <- stack";
        }
        case CODE_LEN_OUTPUT: {
          int64_t out_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
          return output_names_[(IndexTypeOf<int64_t>)out_num] + " len";
        }
        case CODE_REWIND: {
          int64_t out_num = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
          return output_names_[(IndexTypeOf<int64_t>)out_num] + " rewind";
        }
        case CODE_I: {
          return "i";
        }
        case CODE_J: {
          return "j";
        }
        case CODE_K: {
          return "k";
        }
        case CODE_DUP: {
          return "dup";
        }
        case CODE_DROP: {
          return "drop";
        }
        case CODE_SWAP: {
          return "swap";
        }
        case CODE_OVER: {
          return "over";
        }
        case CODE_ROT: {
          return "rot";
        }
        case CODE_NIP: {
          return "nip";
        }
        case CODE_TUCK: {
          return "tuck";
        }
        case CODE_ADD: {
          return "+";
        }
        case CODE_SUB: {
          return "-";
        }
        case CODE_MUL: {
          return "*";
        }
        case CODE_DIV: {
          return "/";
        }
        case CODE_MOD: {
          return "mod";
        }
        case CODE_DIVMOD: {
          return "/mod";
        }
        case CODE_NEGATE: {
          return "negate";
        }
        case CODE_ADD1: {
          return "1+";
        }
        case CODE_SUB1: {
          return "1-";
        }
        case CODE_ABS: {
          return "abs";
        }
        case CODE_MIN: {
          return "min";
        }
        case CODE_MAX: {
          return "max";
        }
        case CODE_EQ: {
          return "=";
        }
        case CODE_NE: {
          return "<>";
        }
        case CODE_GT: {
          return ">";
        }
        case CODE_GE: {
          return ">=";
        }
        case CODE_LT: {
          return "<";
        }
        case CODE_LE: {
          return "<=";
        }
        case CODE_EQ0: {
          return "0=";
        }
        case CODE_INVERT: {
          return "invert";
        }
        case CODE_AND: {
          return "and";
        }
        case CODE_OR: {
          return "or";
        }
        case CODE_XOR: {
          return "xor";
        }
        case CODE_LSHIFT: {
          return "lshift";
        }
        case CODE_RSHIFT: {
          return "rshift";
        }
        case CODE_FALSE: {
          return "false";
        }
        case CODE_TRUE: {
          return "true";
        }
      }
      return std::string("(unrecognized bytecode ") + std::to_string(bytecode) + ")";
    }
  }

  template <typename T, typename I>
  const std::vector<std::string>
  ForthMachineOf<T, I>::dictionary() const {
    return dictionary_names_;
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::stack_max_depth() const noexcept {
    return stack_max_depth_;
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::recursion_max_depth() const noexcept {
    return recursion_max_depth_;
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::output_initial_size() const noexcept {
    return output_initial_size_;
  }

  template <typename T, typename I>
  double
  ForthMachineOf<T, I>::output_resize_factor() const noexcept {
    return output_resize_factor_;
  }

  template <typename T, typename I>
  const std::vector<T>
  ForthMachineOf<T, I>::stack() const {
    std::vector<T> out;
    for (int64_t i = 0;  i < stack_depth_;  i++) {
      out.push_back(stack_buffer_[i]);
    }
    return out;
  }

  template <typename T, typename I>
  T
  ForthMachineOf<T, I>::stack_at(int64_t from_top) const noexcept {
    return stack_buffer_[stack_depth_ - from_top];
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::stack_depth() const noexcept {
    return stack_depth_;
  }

  template <typename T, typename I>
  void
  ForthMachineOf<T, I>::stack_clear() noexcept {
    stack_depth_ = 0;
  }

  template <typename T, typename I>
  const std::map<std::string, T>
  ForthMachineOf<T, I>::variables() const {
    std::map<std::string, T> out;
    for (IndexTypeOf<int64_t> i = 0;  i < variable_names_.size();  i++) {
      out[variable_names_[i]] = variables_[i];
    }
    return out;
  }

  template <typename T, typename I>
  const std::vector<std::string>
  ForthMachineOf<T, I>::variable_index() const {
    return variable_names_;
  }

  template <typename T, typename I>
  T
  ForthMachineOf<T, I>::variable_at(const std::string& name) const {
    for (IndexTypeOf<int64_t> i = 0;  i < variable_names_.size();  i++) {
      if (variable_names_[i] == name) {
        return variables_[i];
      }
    }
    throw std::invalid_argument(
      std::string("variable not found: ") + name + FILENAME(__LINE__)
    );
  }

  template <typename T, typename I>
  T
  ForthMachineOf<T, I>::variable_at(int64_t index) const noexcept {
    return variables_[(IndexTypeOf<int64_t>)index];
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::input_position_at(const std::string& name) const {
    if (!is_ready()) {
      throw std::invalid_argument(
        std::string("need to 'begin' or 'run' to assign inputs") + FILENAME(__LINE__)
      );
    }
    for (IndexTypeOf<int64_t> i = 0;  i < input_names_.size();  i++) {
      if (input_names_[i] == name) {
        return current_inputs_[i].get()->pos();
      }
    }
    throw std::invalid_argument(
      std::string("variable not found: ") + name + FILENAME(__LINE__)
    );
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::input_position_at(int64_t index) const noexcept {
    if (!is_ready()) {
      return -1;
    }
    else {
      return current_inputs_[(IndexTypeOf<int64_t>)index].get()->pos();
    }
  }

  template <typename T, typename I>
  const std::map<std::string, std::shared_ptr<ForthOutputBuffer>>
  ForthMachineOf<T, I>::outputs() const {
    if (!is_ready()) {
      throw std::invalid_argument(
        std::string("need to 'begin' or 'run' to create outputs") + FILENAME(__LINE__)
      );
    }
    std::map<std::string, std::shared_ptr<ForthOutputBuffer>> out;
    for (IndexTypeOf<int64_t> i = 0;  i < output_names_.size();  i++) {
      out[output_names_[i]] = current_outputs_[i];
    }
    return out;
  }

  template <typename T, typename I>
  const std::vector<std::string>
  ForthMachineOf<T, I>::output_index() const noexcept {
    return output_names_;
  }

  template <typename T, typename I>
  const std::shared_ptr<ForthOutputBuffer>
  ForthMachineOf<T, I>::output_at(const std::string& name) const {
    if (!is_ready()) {
      throw std::invalid_argument(
        std::string("need to 'begin' or 'run' to create outputs") + FILENAME(__LINE__)
      );
    }
    for (IndexTypeOf<int64_t> i = 0;  i < output_names_.size();  i++) {
      if (output_names_[i] == name) {
        return current_outputs_[i];
      }
    }
    throw std::invalid_argument(
      std::string("output not found: ") + name + FILENAME(__LINE__)
    );
  }

  template <typename T, typename I>
  const std::shared_ptr<ForthOutputBuffer>
  ForthMachineOf<T, I>::output_at(int64_t index) const noexcept {
    return current_outputs_[(IndexTypeOf<int64_t>)index];
  }

  template <typename T, typename I>
  const ContentPtr
  ForthMachineOf<T, I>::output_NumpyArray_at(const std::string& name) const {
    if (!is_ready()) {
      throw std::invalid_argument(
        std::string("need to 'begin' or 'run' to create outputs") + FILENAME(__LINE__)
      );
    }
    for (IndexTypeOf<int64_t> i = 0;  i < output_names_.size();  i++) {
      if (output_names_[i] == name) {
        return current_outputs_[i].get()->toNumpyArray();
      }
    }
    throw std::invalid_argument(
      std::string("output not found: ") + name + FILENAME(__LINE__)
    );
  }

  template <typename T, typename I>
  const ContentPtr
  ForthMachineOf<T, I>::output_NumpyArray_at(int64_t index) const {
    return current_outputs_[(IndexTypeOf<int64_t>)index].get()->toNumpyArray();
  }

  template <typename T, typename I>
  const Index8
  ForthMachineOf<T, I>::output_Index8_at(const std::string& name) const {
    if (!is_ready()) {
      throw std::invalid_argument(
        std::string("need to 'begin' or 'run' to create outputs") + FILENAME(__LINE__)
      );
    }
    for (IndexTypeOf<int64_t> i = 0;  i < output_names_.size();  i++) {
      if (output_names_[i] == name) {
        return current_outputs_[i].get()->toIndex8();
      }
    }
    throw std::invalid_argument(
      std::string("output not found: ") + name + FILENAME(__LINE__)
    );
  }

  template <typename T, typename I>
  const Index8
  ForthMachineOf<T, I>::output_Index8_at(int64_t index) const {
    return current_outputs_[(IndexTypeOf<int64_t>)index].get()->toIndex8();
  }

  template <typename T, typename I>
  const IndexU8
  ForthMachineOf<T, I>::output_IndexU8_at(const std::string& name) const {
    if (!is_ready()) {
      throw std::invalid_argument(
        std::string("need to 'begin' or 'run' to create outputs") + FILENAME(__LINE__)
      );
    }
    for (IndexTypeOf<int64_t> i = 0;  i < output_names_.size();  i++) {
      if (output_names_[i] == name) {
        return current_outputs_[i].get()->toIndexU8();
      }
    }
    throw std::invalid_argument(
      std::string("output not found: ") + name + FILENAME(__LINE__)
    );
  }

  template <typename T, typename I>
  const IndexU8
  ForthMachineOf<T, I>::output_IndexU8_at(int64_t index) const {
    return current_outputs_[(IndexTypeOf<int64_t>)index].get()->toIndexU8();
  }

  template <typename T, typename I>
  const Index32
  ForthMachineOf<T, I>::output_Index32_at(const std::string& name) const {
    if (!is_ready()) {
      throw std::invalid_argument(
        std::string("need to 'begin' or 'run' to create outputs") + FILENAME(__LINE__)
      );
    }
    for (IndexTypeOf<int64_t> i = 0;  i < output_names_.size();  i++) {
      if (output_names_[i] == name) {
        return current_outputs_[i].get()->toIndex32();
      }
    }
    throw std::invalid_argument(
      std::string("output not found: ") + name + FILENAME(__LINE__)
    );
  }

  template <typename T, typename I>
  const Index32
  ForthMachineOf<T, I>::output_Index32_at(int64_t index) const {
    return current_outputs_[(IndexTypeOf<int64_t>)index].get()->toIndex32();
  }

  template <typename T, typename I>
  const IndexU32
  ForthMachineOf<T, I>::output_IndexU32_at(const std::string& name) const {
    if (!is_ready()) {
      throw std::invalid_argument(
        std::string("need to 'begin' or 'run' to create outputs") + FILENAME(__LINE__)
      );
    }
    for (IndexTypeOf<int64_t> i = 0;  i < output_names_.size();  i++) {
      if (output_names_[i] == name) {
        return current_outputs_[i].get()->toIndexU32();
      }
    }
    throw std::invalid_argument(
      std::string("output not found: ") + name + FILENAME(__LINE__)
    );
  }

  template <typename T, typename I>
  const IndexU32
  ForthMachineOf<T, I>::output_IndexU32_at(int64_t index) const {
    return current_outputs_[(IndexTypeOf<int64_t>)index].get()->toIndexU32();
  }

  template <typename T, typename I>
  const Index64
  ForthMachineOf<T, I>::output_Index64_at(const std::string& name) const {
    if (!is_ready()) {
      throw std::invalid_argument(
        std::string("need to 'begin' or 'run' to create outputs") + FILENAME(__LINE__)
      );
    }
    for (IndexTypeOf<int64_t> i = 0;  i < output_names_.size();  i++) {
      if (output_names_[i] == name) {
        return current_outputs_[i].get()->toIndex64();
      }
    }
    throw std::invalid_argument(
      std::string("output not found: ") + name + FILENAME(__LINE__)
    );
  }

  template <typename T, typename I>
  const Index64
  ForthMachineOf<T, I>::output_Index64_at(int64_t index) const {
    return current_outputs_[(IndexTypeOf<int64_t>)index].get()->toIndex64();
  }

  template <typename T, typename I>
  void
  ForthMachineOf<T, I>::reset() {
    stack_depth_ = 0;
    for (IndexTypeOf<int64_t> i = 0;  i < variables_.size();  i++) {
      variables_[i] = 0;
    }
    current_inputs_.clear();
    current_outputs_.clear();
    is_ready_ = false;
    recursion_current_depth_ = 0;
    while (!recursion_target_depth_.empty()) {
      recursion_target_depth_.pop();
    }
    do_current_depth_ = 0;
    current_error_ = util::ForthError::none;
  }

  template <typename T, typename I>
  void
  ForthMachineOf<T, I>::begin(
      const std::map<std::string, std::shared_ptr<ForthInputBuffer>>& inputs) {

    reset();

    current_inputs_ = std::vector<std::shared_ptr<ForthInputBuffer>>();
    for (auto name : input_names_) {
      bool found = false;
      for (auto pair : inputs) {
        if (pair.first == name) {
          current_inputs_.push_back(pair.second);
          found = true;
          break;
        }
      }
      if (!found) {
        throw std::invalid_argument(
          std::string("AwkwardForth source code defines an input that was not provided: ")
          + name + FILENAME(__LINE__)
        );
      }
    }

    current_outputs_ = std::vector<std::shared_ptr<ForthOutputBuffer>>();
    int64_t init = output_initial_size_;
    double resize = output_resize_factor_;
    for (IndexTypeOf<int64_t> i = 0;  i < output_names_.size();  i++) {
      std::shared_ptr<ForthOutputBuffer> out;
      switch (output_dtypes_[i]) {
        case util::dtype::boolean: {
          out = std::make_shared<ForthOutputBufferOf<bool>>(init, resize);
          break;
        }
        case util::dtype::int8: {
          out = std::make_shared<ForthOutputBufferOf<int8_t>>(init, resize);
          break;
        }
        case util::dtype::int16: {
          out = std::make_shared<ForthOutputBufferOf<int16_t>>(init, resize);
          break;
        }
        case util::dtype::int32: {
          out = std::make_shared<ForthOutputBufferOf<int32_t>>(init, resize);
          break;
        }
        case util::dtype::int64: {
          out = std::make_shared<ForthOutputBufferOf<int64_t>>(init, resize);
          break;
        }
        case util::dtype::uint8: {
          out = std::make_shared<ForthOutputBufferOf<uint8_t>>(init, resize);
          break;
        }
        case util::dtype::uint16: {
          out = std::make_shared<ForthOutputBufferOf<uint16_t>>(init, resize);
          break;
        }
        case util::dtype::uint32: {
          out = std::make_shared<ForthOutputBufferOf<uint32_t>>(init, resize);
          break;
        }
        case util::dtype::uint64: {
          out = std::make_shared<ForthOutputBufferOf<uint64_t>>(init, resize);
          break;
        }
        case util::dtype::float32: {
          out = std::make_shared<ForthOutputBufferOf<float>>(init, resize);
          break;
        }
        case util::dtype::float64: {
          out = std::make_shared<ForthOutputBufferOf<double>>(init, resize);
          break;
        }
        default: {
          throw std::runtime_error(std::string("unhandled ForthOutputBuffer type")
                                   + FILENAME(__LINE__));
        }
      }
      current_outputs_.push_back(out);
    }

    recursion_target_depth_.push(0);
    bytecodes_pointer_push(0);
    is_ready_ = true;
  }

  template <typename T, typename I>
  void
  ForthMachineOf<T, I>::begin() {
    const std::map<std::string, std::shared_ptr<ForthInputBuffer>> inputs;
    begin(inputs);
  }

  template <typename T, typename I>
  util::ForthError
  ForthMachineOf<T, I>::step() {
    if (!is_ready()) {
      current_error_ = util::ForthError::not_ready;
      return current_error_;
    }
    if (is_done()) {
      current_error_ = util::ForthError::is_done;
      return current_error_;
    }
    if (current_error_ != util::ForthError::none) {
      return current_error_;
    }

    int64_t recursion_target_depth_top = recursion_target_depth_.top();

    auto begin_time = std::chrono::high_resolution_clock::now();
    internal_run(true, recursion_target_depth_top);
    auto end_time = std::chrono::high_resolution_clock::now();

    count_nanoseconds_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_time - begin_time
    ).count();

    if (recursion_current_depth_ == recursion_target_depth_.top()) {
      recursion_target_depth_.pop();
    }

    return current_error_;
  }

  template <typename T, typename I>
  util::ForthError
  ForthMachineOf<T, I>::run(
      const std::map<std::string, std::shared_ptr<ForthInputBuffer>>& inputs) {
    begin(inputs);

    int64_t recursion_target_depth_top = recursion_target_depth_.top();

    auto begin_time = std::chrono::high_resolution_clock::now();
    internal_run(false, recursion_target_depth_top);
    auto end_time = std::chrono::high_resolution_clock::now();

    count_nanoseconds_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_time - begin_time
    ).count();

    if (recursion_current_depth_ == recursion_target_depth_.top()) {
      recursion_target_depth_.pop();
    }

    return current_error_;
  }

  template <typename T, typename I>
  util::ForthError
  ForthMachineOf<T, I>::run() {
    const std::map<std::string, std::shared_ptr<ForthInputBuffer>> inputs;
    return run(inputs);
  }

  template <typename T, typename I>
  util::ForthError
  ForthMachineOf<T, I>::resume() {
    if (!is_ready()) {
      current_error_ = util::ForthError::not_ready;
      return current_error_;
    }
    if (is_done()) {
      current_error_ = util::ForthError::is_done;
      return current_error_;
    }
    if (current_error_ != util::ForthError::none) {
      return current_error_;
    }

    int64_t recursion_target_depth_top = recursion_target_depth_.top();

    auto begin_time = std::chrono::high_resolution_clock::now();
    internal_run(false, recursion_target_depth_top);
    auto end_time = std::chrono::high_resolution_clock::now();

    count_nanoseconds_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_time - begin_time
    ).count();

    if (recursion_current_depth_ == recursion_target_depth_.top()) {
      recursion_target_depth_.pop();
    }

    return current_error_;
  }

  template <typename T, typename I>
  util::ForthError
  ForthMachineOf<T, I>::call(const std::string& name) {
    for (IndexTypeOf<int64_t> i = 0;  i < dictionary_names_.size();  i++) {
      if (dictionary_names_[i] == name) {
        return call((int64_t)i);
      }
    }
    throw std::runtime_error(
      std::string("AwkwardForth unrecognized word: ") + name + FILENAME(__LINE__)
    );
  }

  template <typename T, typename I>
  util::ForthError
  ForthMachineOf<T, I>::call(int64_t index) {
    if (!is_ready()) {
      current_error_ = util::ForthError::not_ready;
      return current_error_;
    }
    if (current_error_ != util::ForthError::none) {
      return current_error_;
    }

    recursion_target_depth_.push(recursion_current_depth_);
    bytecodes_pointer_push(dictionary_bytecodes_[(IndexTypeOf<int64_t>)index] - BOUND_DICTIONARY);

    int64_t recursion_target_depth_top = recursion_target_depth_.top();

    auto begin_time = std::chrono::high_resolution_clock::now();
    internal_run(false, recursion_target_depth_top);
    auto end_time = std::chrono::high_resolution_clock::now();

    count_nanoseconds_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
        end_time - begin_time
    ).count();

    if (recursion_current_depth_ == recursion_target_depth_.top()) {
      recursion_target_depth_.pop();
    }

    return current_error_;
  }

  template <typename T, typename I>
  void
  ForthMachineOf<T, I>::maybe_throw(util::ForthError err,
                                    const std::set<util::ForthError>& ignore) const {
    if (ignore.count(current_error_) == 0) {
      switch (current_error_) {
        case util::ForthError::not_ready: {
          throw std::invalid_argument(
            "'not ready' in AwkwardForth runtime: call 'begin' before 'step' or "
            "'resume' (note: check 'is_ready')");
        }
        case util::ForthError::is_done: {
          throw std::invalid_argument(
            "'is done' in AwkwardForth runtime: reached the end of the program; "
            "call 'begin' to 'step' again (note: check 'is_done')");
        }
        case util::ForthError::user_halt: {
          throw std::invalid_argument(
            "'user halt' in AwkwardForth runtime: user-defined error or stopping "
            "condition");
        }
        case util::ForthError::recursion_depth_exceeded: {
          throw std::invalid_argument(
            "'recursion depth exceeded' in AwkwardForth runtime: too many words "
            "calling words or a recursive word is looping endlessly");
        }
        case util::ForthError::stack_underflow: {
          throw std::invalid_argument(
            "'stack underflow' in AwkwardForth runtime: tried to pop from an empty "
            "stack");
        }
        case util::ForthError::stack_overflow: {
          throw std::invalid_argument(
            "'stack overflow' in AwkwardForth runtime: tried to push beyond the "
            "predefined maximum stack depth");
        }
        case util::ForthError::read_beyond: {
          throw std::invalid_argument(
            "'read beyond' in AwkwardForth runtime: tried to read beyond the end "
            "of an input");
        }
        case util::ForthError::seek_beyond: {
          throw std::invalid_argument(
            "'seek beyond' in AwkwardForth runtime: tried to seek beyond the bounds "
            "of an input (0 or length)");
        }
        case util::ForthError::skip_beyond: {
          throw std::invalid_argument(
            "'skip beyond' in AwkwardForth runtime: tried to skip beyond the bounds "
            "of an input (0 or length)");
        }
        case util::ForthError::rewind_beyond: {
          throw std::invalid_argument(
            "'rewind beyond' in AwkwardForth runtime: tried to rewind beyond the "
            "beginning of an output");
        }
        case util::ForthError::division_by_zero: {
          throw std::invalid_argument(
            "'division by zero' in AwkwardForth runtime: tried to divide by zero");
        }
        default:
          break;
      }
    }
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::current_bytecode_position() const noexcept {
    if (recursion_current_depth_ == 0) {
      return -1;
    }
    else {
      int64_t which = current_which_[recursion_current_depth_ - 1];
      int64_t where = current_where_[recursion_current_depth_ - 1];
      if (where < bytecodes_offsets_[which + 1] - bytecodes_offsets_[which]) {
        return bytecodes_offsets_[which] + where;
      }
      else {
        return -1;
      }
    }
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::current_recursion_depth() const noexcept {
    if (recursion_target_depth_.empty()) {
      return -1;
    }
    else {
      return recursion_current_depth_ - recursion_target_depth_.top();
    }
  }

  template <typename T, typename I>
  const std::string
  ForthMachineOf<T, I>::current_instruction() const {
    int64_t bytecode_position = current_bytecode_position();
    if (bytecode_position == -1) {
      throw std::invalid_argument(
        "'is done' in AwkwardForth runtime: reached the end of the program or segment; "
        "call 'begin' to 'step' again (note: check 'is_done')"
        + FILENAME(__LINE__)
      );
    }
    else {
      return decompiled_at(bytecode_position, "");
    }
  }

  template <typename T, typename I>
  void
  ForthMachineOf<T, I>::count_reset() noexcept {
    count_instructions_ = 0;
    count_reads_ = 0;
    count_writes_ = 0;
    count_nanoseconds_ = 0;
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::count_instructions() const noexcept {
    return count_instructions_;
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::count_reads() const noexcept {
    return count_reads_;
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::count_writes() const noexcept {
    return count_writes_;
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::count_nanoseconds() const noexcept {
    return count_nanoseconds_;
  }

  template <typename T, typename I>
  bool
  ForthMachineOf<T, I>::is_integer(const std::string& word, int64_t& value) const {
    if (word.size() >= 2  &&  word.substr(0, 2) == std::string("0x")) {
      try {
        value = (int64_t)std::stoul(word.substr(2, word.size() - 2), nullptr, 16);
      }
      catch (std::invalid_argument err) {
        return false;
      }
      return true;
    }
    else {
      try {
        value = (int64_t)std::stoul(word, nullptr, 10);
      }
      catch (std::invalid_argument err) {
        return false;
      }
      return true;
    }
  }

  template <typename T, typename I>
  bool
  ForthMachineOf<T, I>::is_variable(const std::string& word) const {
    return std::find(variable_names_.begin(),
                     variable_names_.end(), word) != variable_names_.end();
  }

  template <typename T, typename I>
  bool
  ForthMachineOf<T, I>::is_input(const std::string& word) const {
    return std::find(input_names_.begin(),
                     input_names_.end(), word) != input_names_.end();
  }

  template <typename T, typename I>
  bool
  ForthMachineOf<T, I>::is_output(const std::string& word) const {
    return std::find(output_names_.begin(),
                     output_names_.end(), word) != output_names_.end();
  }

  template <typename T, typename I>
  bool
  ForthMachineOf<T, I>::is_reserved(const std::string& word) const {
    return reserved_words_.find(word) != reserved_words_.end()  ||
           input_parser_words_.find(word) != input_parser_words_.end()  ||
           output_dtype_words_.find(word) != output_dtype_words_.end()  ||
           generic_builtin_words_.find(word) != generic_builtin_words_.end();
  }

  template <typename T, typename I>
  bool
  ForthMachineOf<T, I>::is_defined(const std::string& word) const {
    for (auto name : dictionary_names_) {
      if (name == word) {
        return true;
      }
    }
    return false;
  }

  template <typename T, typename I>
  bool
  ForthMachineOf<T, I>::segment_nonempty(int64_t segment_position) const {
    return bytecodes_offsets_[(IndexTypeOf<int64_t>)segment_position] != bytecodes_offsets_[(IndexTypeOf<int64_t>)segment_position + 1];
  }

  template <typename T, typename I>
  int64_t
  ForthMachineOf<T, I>::bytecodes_per_instruction(int64_t bytecode_position) const {
    I bytecode = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position];
    I next_bytecode = 0;
    if ((IndexTypeOf<int64_t>)bytecode_position + 1 < bytecodes_.size()) {
      next_bytecode = bytecodes_[(IndexTypeOf<int64_t>)bytecode_position + 1];
    }

    if (bytecode < 0) {
      if (~bytecode & READ_DIRECT) {
        return 3;
      }
      else {
        return 2;
      }
    }
    else if (next_bytecode == CODE_AGAIN  ||  next_bytecode == CODE_UNTIL) {
      return 2;
    }
    else if (next_bytecode == CODE_WHILE) {
      return 3;
    }
    else {
      switch (bytecode) {
        case CODE_IF_ELSE:
          return 3;
        case CODE_LITERAL:
        case CODE_IF:
        case CODE_DO:
        case CODE_DO_STEP:
        case CODE_EXIT:
        case CODE_PUT:
        case CODE_INC:
        case CODE_GET:
        case CODE_LEN_INPUT:
        case CODE_POS:
        case CODE_END:
        case CODE_SEEK:
        case CODE_SKIP:
        case CODE_WRITE:
        case CODE_LEN_OUTPUT:
        case CODE_REWIND:
          return 2;
        default:
          return 1;
      }
    }
  }

  template <typename T, typename I>
  const std::string
  ForthMachineOf<T, I>::err_linecol(const std::vector<std::pair<int64_t, int64_t>>& linecol,
                                    int64_t startpos,
                                    int64_t stoppos,
                                    const std::string& message) const {
    std::pair<int64_t, int64_t> lc = linecol[(IndexTypeOf<int64_t>)startpos];
    std::stringstream out;
    out << "in AwkwardForth source code, line " << lc.first << " col " << lc.second
        << ", " << message << ":" << std::endl << std::endl << "    ";
    int64_t line = 1;
    int64_t col = 1;
    IndexTypeOf<int64_t> start = 0;
    IndexTypeOf<int64_t> stop = 0;
    while (stop < source_.length()) {
      if (lc.first == line  &&  lc.second == col) {
        start = stop;
      }
      if ((IndexTypeOf<int64_t>)stoppos < linecol.size()  &&
          linecol[(IndexTypeOf<int64_t>)stoppos].first == line  &&  linecol[(IndexTypeOf<int64_t>)stoppos].second == col) {
        break;
      }
      if (source_[stop] == '\n') {
        line++;
        col = 0;
      }
      col++;
      stop++;
    }
    out << source_.substr(start, stop - start);
    return out.str();
  }

  template <typename T, typename I>
  void ForthMachineOf<T, I>::tokenize(std::vector<std::string>& tokenized,
                                      std::vector<std::pair<int64_t, int64_t>>& linecol) {
    IndexTypeOf<int64_t> start = 0;
    IndexTypeOf<int64_t> stop = 0;
    bool full = false;
    int64_t line = 1;
    int64_t colstart = 0;
    int64_t colstop = 0;
    while (stop < source_.size()) {
      char current = source_[stop];
      // Whitespace separates tokens and is not included in them.
      if (current == ' '  ||  current == '\r'  ||  current == '\t'  ||
          current == '\v'  ||  current == '\f') {
        if (full) {
          tokenized.push_back(source_.substr(start, stop - start));
          linecol.push_back(std::pair<int64_t, int64_t>(line, colstart));
        }
        start = stop;
        full = false;
        colstart = colstop;
      }
      // '\n' is considered a token because it terminates '\\ .. \n' comments.
      // It has no semantic meaning after the parsing stage.
      else if (current == '\n') {
        if (full) {
          tokenized.push_back(source_.substr(start, stop - start));
          linecol.push_back(std::pair<int64_t, int64_t>(line, colstart));
        }
        tokenized.push_back(source_.substr(stop, 1));
        linecol.push_back(std::pair<int64_t, int64_t>(line, colstart));
        start = stop;
        full = false;
        line++;
        colstart = 0;
        colstop = 0;
      }
      // Everything else is part of a token (Forth word).
      else {
        if (!full) {
          start = stop;
          colstart = colstop;
        }
        full = true;
      }
      stop++;
      colstop++;
    }
    // The source code might end on non-whitespace.
    if (full) {
      tokenized.push_back(source_.substr(start, stop - start));
      linecol.push_back(std::pair<int64_t, int64_t>(line, colstart));
    }
  }

  template <typename T, typename I>
  void ForthMachineOf<T, I>::compile(const std::vector<std::string>& tokenized,
                                     const std::vector<std::pair<int64_t, int64_t>>& linecol) {
    std::vector<std::vector<I>> dictionary;

    // Start recursive parsing.
    std::vector<I> bytecodes;
    dictionary.push_back(bytecodes);
    parse("",
          tokenized,
          linecol,
          0,
          (int64_t)tokenized.size(),
          bytecodes,
          dictionary,
          0,
          0);
    dictionary[0] = bytecodes;

    // Copy std::vector<std::vector<I>> to flattened contents and offsets.
    bytecodes_offsets_.push_back(0);
    for (auto segment : dictionary) {
      for (auto bytecode : segment) {
        bytecodes_.push_back(bytecode);
      }
      bytecodes_offsets_.push_back((int64_t)bytecodes_.size());
    }
  }

  template <typename T, typename I>
  void
  ForthMachineOf<T, I>::parse(const std::string& defn,
                              const std::vector<std::string>& tokenized,
                              const std::vector<std::pair<int64_t, int64_t>>& linecol,
                              int64_t start,
                              int64_t stop,
                              std::vector<I>& bytecodes,
                              std::vector<std::vector<I>>& dictionary,
                              int64_t exitdepth,
                              int64_t dodepth) {
    int64_t pos = start;
    while (pos < stop) {
      std::string word = tokenized[(IndexTypeOf<std::string>)pos];

      if (word == "(") {
        // Simply skip the parenthesized text: it's a comment.
        int64_t substop = pos;
        int64_t nesting = 1;
        while (nesting > 0) {
          substop++;
          if (substop >= stop) {
            throw std::invalid_argument(
              err_linecol(linecol, pos, substop, "'(' is missing its closing ')'")
              + FILENAME(__LINE__)
            );
          }
          // Any parentheses in the comment text itself must be balanced.
          if (tokenized[(IndexTypeOf<std::string>)substop] == "(") {
            nesting++;
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == ")") {
            nesting--;
          }
        }

        pos = substop + 1;
      }

      else if (word == "\\") {
        // Modern, backslash-to-end-of-line comments. Nothing needs to be balanced.
        int64_t substop = pos;
        while (substop < stop  &&  tokenized[(IndexTypeOf<std::string>)substop] != "\n") {
          substop++;
        }

        pos = substop + 1;
      }

      else if (word == "\n") {
        // This is a do-nothing token to delimit backslash-to-end-of-line comments.
        pos++;
      }

      else if (word == "") {
        // Just in case there's a leading or trailing blank in the token stream.
        pos++;
      }

      else if (word == ":") {
        if (pos + 1 >= stop  ||  tokenized[(IndexTypeOf<std::string>)pos + 1] == ";") {
            throw std::invalid_argument(
              err_linecol(linecol, pos, pos + 2, "missing name in word definition")
              + FILENAME(__LINE__)
            );
        }
        std::string name = tokenized[(IndexTypeOf<std::string>)pos + 1];

        int64_t num;
        if (is_input(name)  ||  is_output(name)  ||  is_variable(name)  ||
            is_defined(name)  ||  is_reserved(name)  ||  is_integer(name, num)) {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 2,
                        "input names, output names, variable names, and user-defined"
                        "words must all be unique and not reserved words or integers")
            + FILENAME(__LINE__)
          );
        }

        int64_t substart = pos + 2;
        int64_t substop = pos + 1;
        int64_t nesting = 1;
        while (nesting > 0) {
          substop++;
          if (substop >= stop) {
            throw std::invalid_argument(
              err_linecol(linecol, pos, stop,
                          "definition is missing its closing ';'")
              + FILENAME(__LINE__)
            );
          }
          if (tokenized[(IndexTypeOf<std::string>)substop] == ":") {
            nesting++;
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == ";") {
            nesting--;
          }
        }

        // Add the new word to the dictionary before parsing it so that recursive
        // functions can be defined.
        I bytecode = (I)dictionary.size() + BOUND_DICTIONARY;
        dictionary_names_.push_back(name);
        dictionary_bytecodes_.push_back(bytecode);

        // Now parse the subroutine and add it to the dictionary.
        std::vector<I> body;
        dictionary.push_back(body);
        parse(name,
              tokenized,
              linecol,
              substart,
              substop,
              body,
              dictionary,
              0,
              0);
        dictionary[(IndexTypeOf<I>)bytecode - BOUND_DICTIONARY] = body;

        pos = substop + 1;
      }

      else if (word == "recurse") {
        if (defn == "") {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 1,
                        "only allowed in a ': name ... ;' definition")
              + FILENAME(__LINE__)
          );
        }
        for (IndexTypeOf<I> i = 0;  i < dictionary_names_.size();  i++) {
          if (dictionary_names_[i] == defn) {
            bytecodes.push_back(dictionary_bytecodes_[i]);
          }
        }

        pos++;
      }

      else if (word == "variable") {
        if (pos + 1 >= stop) {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 2,
                        "missing name in variable declaration")
            + FILENAME(__LINE__)
          );
        }
        std::string name = tokenized[(IndexTypeOf<std::string>)pos + 1];

        int64_t num;
        if (is_input(name)  ||  is_output(name)  ||  is_variable(name)  ||
            is_defined(name)  ||  is_reserved(name)  ||  is_integer(name, num)) {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 2,
                        "input names, output names, variable names, and user-defined"
                        "words must all be unique and not reserved words or integers")
            + FILENAME(__LINE__)
          );
        }

        variable_names_.push_back(name);
        variables_.push_back(0);

        pos += 2;
      }

      else if (word == "input") {
        if (pos + 1 >= stop) {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 2, "missing name in input declaration")
            + FILENAME(__LINE__)
          );
        }
        std::string name = tokenized[(IndexTypeOf<std::string>)pos + 1];

        int64_t num;
        if (is_input(name)  ||  is_output(name)  ||  is_variable(name)  ||
            is_defined(name)  ||  is_reserved(name)  ||  is_integer(name, num)) {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 2,
                        "input names, output names, variable names, and user-defined"
                        "words must all be unique and not reserved words or integers")
            + FILENAME(__LINE__)
          );
        }

        input_names_.push_back(name);

        pos += 2;
      }

      else if (word == "output") {
        if (pos + 2 >= stop) {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 3,
                        "missing name or dtype in output declaration")
            + FILENAME(__LINE__)
          );
        }
        std::string name = tokenized[(IndexTypeOf<std::string>)pos + 1];
        std::string dtype_string = tokenized[(IndexTypeOf<std::string>)pos + 2];

        int64_t num;
        if (is_input(name)  ||  is_output(name)  ||  is_variable(name)  ||
            is_defined(name)  ||  is_reserved(name)  ||  is_integer(name, num)) {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 2,
                        "input names, output names, variable names, and user-defined"
                        "words must all be unique and not reserved words or integers")
            + FILENAME(__LINE__)
          );
        }

        bool found_dtype = false;
        for (auto pair : output_dtype_words_) {
          if (pair.first == dtype_string) {
            output_names_.push_back(name);
            output_dtypes_.push_back(pair.second);
            found_dtype = true;
            break;
          }
        }
        if (!found_dtype) {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 3, "output dtype not recognized")
            + FILENAME(__LINE__)
          );
        }

        pos += 3;
      }

      else if (word == "halt") {
        bytecodes.push_back(CODE_HALT);

        pos++;
      }

      else if (word == "pause") {
        bytecodes.push_back(CODE_PAUSE);

        pos++;
      }

      else if (word == "if") {
        int64_t substart = pos + 1;
        int64_t subelse = -1;
        int64_t substop = pos;
        int64_t nesting = 1;
        while (nesting > 0) {
          substop++;
          if (substop >= stop) {
            throw std::invalid_argument(
              err_linecol(linecol, pos, stop, "'if' is missing its closing 'then'")
              + FILENAME(__LINE__)
            );
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == "if") {
            nesting++;
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == "then") {
            nesting--;
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == "else"  &&  nesting == 1) {
            subelse = substop;
          }
        }

        if (subelse == -1) {
          // Add the consequent to the dictionary so that it can be used
          // without special instruction pointer manipulation at runtime.
          I bytecode = (I)dictionary.size() + BOUND_DICTIONARY;
          std::vector<I> consequent;
          dictionary.push_back(consequent);
          parse(defn,
                tokenized,
                linecol,
                substart,
                substop,
                consequent,
                dictionary,
                exitdepth + 1,
                dodepth);
          dictionary[(IndexTypeOf<int64_t>)bytecode - BOUND_DICTIONARY] = consequent;

          bytecodes.push_back(CODE_IF);
          bytecodes.push_back(bytecode);

          pos = substop + 1;
        }
        else {
          // Same as above, except that two new definitions must be made.
          I bytecode1 = (I)dictionary.size() + BOUND_DICTIONARY;
          std::vector<I> consequent;
          dictionary.push_back(consequent);
          parse(defn,
                tokenized,
                linecol,
                substart,
                subelse,
                consequent,
                dictionary,
                exitdepth + 1,
                dodepth);
          dictionary[(IndexTypeOf<int64_t>)bytecode1 - BOUND_DICTIONARY] = consequent;

          I bytecode2 = (I)dictionary.size() + BOUND_DICTIONARY;
          std::vector<I> alternate;
          dictionary.push_back(alternate);
          parse(defn,
                tokenized,
                linecol,
                subelse + 1,
                substop,
                alternate,
                dictionary,
                exitdepth + 1,
                dodepth);
          dictionary[(IndexTypeOf<int64_t>)bytecode2 - BOUND_DICTIONARY] = alternate;

          bytecodes.push_back(CODE_IF_ELSE);
          bytecodes.push_back(bytecode1);
          bytecodes.push_back(bytecode2);

          pos = substop + 1;
        }
      }

      else if (word == "do") {
        int64_t substart = pos + 1;
        int64_t substop = pos;
        bool is_step = false;
        int64_t nesting = 1;
        while (nesting > 0) {
          substop++;
          if (substop >= stop) {
            throw std::invalid_argument(
              err_linecol(linecol, pos, stop,
                          "'do' is missing its closing 'loop'")
              + FILENAME(__LINE__)
            );
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == "do") {
            nesting++;
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == "loop") {
            nesting--;
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == "+loop") {
            if (nesting == 1) {
              is_step = true;
            }
            nesting--;
          }
        }

        // Add the loop body to the dictionary so that it can be used
        // without special instruction pointer manipulation at runtime.
        I bytecode = (I)dictionary.size() + BOUND_DICTIONARY;
        std::vector<I> body;
        dictionary.push_back(body);
        parse(defn,
              tokenized,
              linecol,
              substart,
              substop,
              body,
              dictionary,
              exitdepth + 1,
              dodepth + 1);
        dictionary[(IndexTypeOf<int64_t>)bytecode - BOUND_DICTIONARY] = body;

        if (is_step) {
          bytecodes.push_back(CODE_DO_STEP);
          bytecodes.push_back(bytecode);
        }
        else {
          bytecodes.push_back(CODE_DO);
          bytecodes.push_back(bytecode);
        }

        pos = substop + 1;
      }

      else if (word == "begin") {
        int64_t substart = pos + 1;
        int64_t substop = pos;
        bool is_again = false;
        int64_t subwhile = -1;
        int64_t nesting = 1;
        while (nesting > 0) {
          substop++;
          if (substop >= stop) {
            throw std::invalid_argument(
              err_linecol(linecol, pos, stop,
                          "'begin' is missing its closing 'until' or 'while ... repeat'")
              + FILENAME(__LINE__)
            );
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == "begin") {
            nesting++;
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == "until") {
            nesting--;
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == "again") {
            if (nesting == 1) {
              is_again = true;
            }
            nesting--;
          }
          else if (tokenized[(IndexTypeOf<std::string>)substop] == "while") {
            if (nesting == 1) {
              subwhile = substop;
            }
            nesting--;
            int64_t subnesting = 1;
            while (subnesting > 0) {
              substop++;
              if (substop >= stop) {
                throw std::invalid_argument(
                  err_linecol(linecol, pos, stop,
                              "'while' is missing its closing 'repeat'")
                  + FILENAME(__LINE__)
                );
              }
              else if (tokenized[(IndexTypeOf<std::string>)substop] == "while") {
                subnesting++;
              }
              else if (tokenized[(IndexTypeOf<std::string>)substop] == "repeat") {
                subnesting--;
              }
            }
          }
        }

        if (is_again) {
          // Add the 'begin ... again' body to the dictionary so that it can be
          // used without special instruction pointer manipulation at runtime.
          I bytecode = (I)dictionary.size() + BOUND_DICTIONARY;
          std::vector<I> body;
          dictionary.push_back(body);
          parse(defn,
                tokenized,
                linecol,
                substart,
                substop,
                body,
                dictionary,
                exitdepth + 1,
                dodepth);
          dictionary[(IndexTypeOf<int64_t>)bytecode - BOUND_DICTIONARY] = body;

          bytecodes.push_back(bytecode);
          bytecodes.push_back(CODE_AGAIN);

          pos = substop + 1;
        }
        else if (subwhile == -1) {
          // Same for the 'begin .. until' body.
          I bytecode = (I)dictionary.size() + BOUND_DICTIONARY;
          std::vector<I> body;
          dictionary.push_back(body);
          parse(defn,
                tokenized,
                linecol,
                substart,
                substop,
                body,
                dictionary,
                exitdepth + 1,
                dodepth);
          dictionary[(IndexTypeOf<int64_t>)bytecode - BOUND_DICTIONARY] = body;

          bytecodes.push_back(bytecode);
          bytecodes.push_back(CODE_UNTIL);

          pos = substop + 1;
        }
        else {
          // Same for the 'begin .. repeat' statements.
          I bytecode1 = (I)dictionary.size() + BOUND_DICTIONARY;
          std::vector<I> precondition;
          dictionary.push_back(precondition);
          parse(defn,
                tokenized,
                linecol,
                substart,
                subwhile,
                precondition,
                dictionary,
                exitdepth + 1,
                dodepth);
          dictionary[(IndexTypeOf<int64_t>)bytecode1 - BOUND_DICTIONARY] = precondition;

          // Same for the 'repeat .. until' statements.
          I bytecode2 = (I)dictionary.size() + BOUND_DICTIONARY;
          std::vector<I> postcondition;
          dictionary.push_back(postcondition);
          parse(defn,
                tokenized,
                linecol,
                subwhile + 1,
                substop,
                postcondition,
                dictionary,
                exitdepth + 1,
                dodepth);
          dictionary[(IndexTypeOf<int64_t>)bytecode2 - BOUND_DICTIONARY] = postcondition;

          bytecodes.push_back(bytecode1);
          bytecodes.push_back(CODE_WHILE);
          bytecodes.push_back(bytecode2);

          pos = substop + 1;
        }
      }

      else if (word == "exit") {
        bytecodes.push_back(CODE_EXIT);
        bytecodes.push_back((int32_t)exitdepth);

        pos++;
      }

      else if (is_variable(word)) {
        IndexTypeOf<int64_t> variable_index = 0;
        for (;  variable_index < variable_names_.size();  variable_index++) {
          if (variable_names_[variable_index] == word) {
            break;
          }
        }
        if (pos + 1 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 1] == "!") {
          bytecodes.push_back(CODE_PUT);
          bytecodes.push_back((int32_t)variable_index);

          pos += 2;
        }
        else if (pos + 1 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 1] == "+!") {
          bytecodes.push_back(CODE_INC);
          bytecodes.push_back((int32_t)variable_index);

          pos += 2;
        }
        else if (pos + 1 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 1] == "@") {
          bytecodes.push_back(CODE_GET);
          bytecodes.push_back((int32_t)variable_index);

          pos += 2;
        }
        else {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 2, "missing '!', '+!', or '@' "
                        "after variable name")
          );
        }
      }

      else if (is_input(word)) {
        IndexTypeOf<I> input_index = 0;
        for (;  input_index < input_names_.size();  input_index++) {
          if (input_names_[input_index] == word) {
            break;
          }
        }

        if (pos + 1 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 1] == "len") {
          bytecodes.push_back(CODE_LEN_INPUT);
          bytecodes.push_back((int32_t)input_index);

          pos += 2;
        }
        else if (pos + 1 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 1] == "pos") {
          bytecodes.push_back(CODE_POS);
          bytecodes.push_back((int32_t)input_index);

          pos += 2;
        }
        else if (pos + 1 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 1] == "end") {
          bytecodes.push_back(CODE_END);
          bytecodes.push_back((int32_t)input_index);

          pos += 2;
        }
        else if (pos + 1 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 1] == "seek") {
          bytecodes.push_back(CODE_SEEK);
          bytecodes.push_back((int32_t)input_index);

          pos += 2;
        }
        else if (pos + 1 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 1] == "skip") {
          bytecodes.push_back(CODE_SKIP);
          bytecodes.push_back((int32_t)input_index);

          pos += 2;
        }
        else if (pos + 1 < stop) {
          I bytecode = 0;

          std::string parser = tokenized[(IndexTypeOf<std::string>)pos + 1];

          if (parser.length() != 0  &&  parser[0] == '#') {
            bytecode |= READ_REPEATED;
            parser = parser.substr(1, parser.length() - 1);
          }

          if (parser.length() != 0  &&  parser[0] == '!') {
            bytecode |= READ_BIGENDIAN;
            parser = parser.substr(1, parser.length() - 1);
          }

          bool good = true;
          if (parser.length() != 0) {
            switch (parser[0]) {
              case '?': {
                bytecode |= READ_BOOL;
                break;
              }
              case 'b': {
                bytecode |= READ_INT8;
                break;
              }
              case 'h': {
                bytecode |= READ_INT16;
                break;
              }
              case 'i': {
                 bytecode |= READ_INT32;
                 break;
               }
              case 'q': {
                 bytecode |= READ_INT64;
                 break;
               }
              case 'n': {
                bytecode |= READ_INTP;
                break;
              }
              case 'B': {
                bytecode |= READ_UINT8;
                break;
              }
              case 'H': {
                bytecode |= READ_UINT16;
                break;
              }
              case 'I': {
                bytecode |= READ_UINT32;
                break;
              }
              case 'Q': {
                bytecode |= READ_UINT64;
                break;
              }
              case 'N': {
                bytecode |= READ_UINTP;
                break;
              }
              case 'f': {
                bytecode |= READ_FLOAT32;
                break;
              }
              case 'd': {
                bytecode |= READ_FLOAT64;
                break;
              }
              default: {
                good = false;
              }
            }
            if (good) {
              parser = parser.substr(1, parser.length() - 1);
            }
          }

          if (!good  ||  parser != "->") {
            throw std::invalid_argument(
              err_linecol(linecol, pos, pos + 3,
                          "missing '*-> stack/output', "
                          "'seek', 'skip', 'end', 'pos', or 'len' after input name")
              + FILENAME(__LINE__)
            );
          }

          bool found_output = false;
          IndexTypeOf<I> output_index = 0;
          if (pos + 2 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 2] == "stack") {
            // not READ_DIRECT
          }
          else if (pos + 2 < stop  &&  is_output(tokenized[(IndexTypeOf<std::string>)pos + 2])) {
            for (;  output_index < output_names_.size();  output_index++) {
              if (output_names_[output_index] == tokenized[(IndexTypeOf<std::string>)pos + 2]) {
                found_output = true;
                break;
              }
            }
            bytecode |= READ_DIRECT;
          }
          else {
            throw std::invalid_argument(
              err_linecol(linecol, pos, pos + 3,
                          "missing 'stack' or 'output' after '*->'")
              + FILENAME(__LINE__)
            );
          }

          // Parser instructions are bit-flipped to detect them by the sign bit.
          bytecodes.push_back(~bytecode);
          bytecodes.push_back((int32_t)input_index);
          if (found_output) {
            bytecodes.push_back((int32_t)output_index);
          }

          pos += 3;
        }
        else {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 3,
                        "missing '*-> stack/output', 'seek', 'skip', 'end', "
                        "'pos', or 'len' after input name")
            + FILENAME(__LINE__)
          );
        }
      }

      else if (is_output(word)) {
        IndexTypeOf<I> output_index = 0;
        for (;  output_index < output_names_.size();  output_index++) {
          if (output_names_[output_index] == word) {
            break;
          }
        }
        if (pos + 1 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 1] == "<-") {
          if (pos + 2 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 2] == "stack") {
            bytecodes.push_back(CODE_WRITE);
            bytecodes.push_back((int32_t)output_index);

            pos += 3;
          }
          else {
            throw std::invalid_argument(
              err_linecol(linecol, pos, pos + 3, "missing 'stack' after '<-'")
              + FILENAME(__LINE__)
            );
          }
        }
        else if (pos + 1 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 1] == "len") {
          bytecodes.push_back(CODE_LEN_OUTPUT);
          bytecodes.push_back((int32_t)output_index);

          pos += 2;
        }
        else if (pos + 1 < stop  &&  tokenized[(IndexTypeOf<std::string>)pos + 1] == "rewind") {
          bytecodes.push_back(CODE_REWIND);
          bytecodes.push_back((int32_t)output_index);

          pos += 2;
        }
        else {
          throw std::invalid_argument(
            err_linecol(linecol, pos, pos + 2, "missing '<- stack', "
                        "'len', or 'rewind' after output name")
            + FILENAME(__LINE__)
          );
        }
      }

      else {
        bool found_in_builtins = false;
        for (auto pair : generic_builtin_words_) {
          if (pair.first == word) {
            found_in_builtins = true;
            if (word == "i"  &&  dodepth < 1) {
              throw std::invalid_argument(
                err_linecol(linecol, pos, pos + 1, "only allowed in a 'do' loop")
                + FILENAME(__LINE__)
              );
            }
            if (word == "j"  &&  dodepth < 2) {
              throw std::invalid_argument(
                err_linecol(linecol, pos, pos + 1, "only allowed in a nested 'do' loop")
                + FILENAME(__LINE__)
              );
            }
            if (word == "k"  &&  dodepth < 3) {
              throw std::invalid_argument(
                err_linecol(linecol, pos, pos + 1, "only allowed in a doubly nested 'do' loop")
                + FILENAME(__LINE__)
              );
            }
            bytecodes.push_back((int32_t)pair.second);

            pos++;
          }
        }

        if (!found_in_builtins) {
          bool found_in_dictionary = false;
          for (IndexTypeOf<std::string> i = 0;  i < dictionary_names_.size();  i++) {
            if (dictionary_names_[i] == word) {
              found_in_dictionary = true;
              bytecodes.push_back((int32_t)dictionary_bytecodes_[i]);

              pos++;
            }
          }

          if (!found_in_dictionary) {
            int64_t num;
            if (is_integer(word, num)) {
              bytecodes.push_back(CODE_LITERAL);
              bytecodes.push_back((int32_t)num);

              pos++;
            }

            else {
              throw std::invalid_argument(
                err_linecol(linecol, pos, pos + 1, "unrecognized word or wrong context for word")
                + FILENAME(__LINE__)
              );
            }
          } // !found_in_dictionary
        } // !found_in_builtins
      } // generic instruction
    } // end loop over segment
  }

  template <typename T, typename I>
  void
  ForthMachineOf<T, I>::internal_run(bool single_step, int64_t recursion_target_depth_top) { // noexcept
    while (recursion_current_depth_ != recursion_target_depth_top) {
      while (bytecodes_pointer_where() < (
                 bytecodes_offsets_[(IndexTypeOf<int64_t>)bytecodes_pointer_which() + 1] -
                 bytecodes_offsets_[(IndexTypeOf<int64_t>)bytecodes_pointer_which()]
             )) {
        I bytecode = bytecode_get();

        if (do_current_depth_ == 0  ||
            do_abs_recursion_depth() != recursion_current_depth_) {
          // Normal operation: step forward one bytecode.
          bytecodes_pointer_where()++;
        }
        else if (do_i() >= do_stop()) {
          // End a 'do' loop.
          do_current_depth_--;
          bytecodes_pointer_where()++;
          continue;
        }
        // else... don't increase bytecode_pointer_where()

        if (bytecode < 0) {
          bool byteswap;
          if (NATIVELY_BIG_ENDIAN) {
            byteswap = ((~bytecode & READ_BIGENDIAN) == 0);
          }
          else {
            byteswap = ((~bytecode & READ_BIGENDIAN) != 0);
          }

          I in_num = bytecode_get();
          bytecodes_pointer_where()++;

          int64_t num_items = 1;
          if (~bytecode & READ_REPEATED) {
            if (stack_cannot_pop()) {
              current_error_ = util::ForthError::stack_underflow;
              return;
            }
            num_items = stack_pop();
          }

          if (~bytecode & READ_DIRECT) {
            I out_num = bytecode_get();
            bytecodes_pointer_where()++;

            #define WRITE_DIRECTLY(TYPE, SUFFIX) {                             \
                TYPE* ptr = reinterpret_cast<TYPE*>(                           \
                    current_inputs_[(IndexTypeOf<int64_t>)in_num].get()->read( \
                      num_items * (int64_t)sizeof(TYPE), current_error_));     \
                if (current_error_ != util::ForthError::none) {                \
                  return;                                                      \
                }                                                              \
                if (num_items == 1) {                                          \
                  current_outputs_[(IndexTypeOf<int64_t>)out_num].get()->write_one_##SUFFIX(\
                      *ptr, byteswap);                                         \
                }                                                              \
                else {                                                         \
                  current_outputs_[(IndexTypeOf<int64_t>)out_num].get()->write_##SUFFIX(   \
                      num_items, ptr, byteswap);                               \
                }                                                              \
                break;                                                         \
              }

            switch (~bytecode & READ_MASK) {
              case READ_BOOL:    WRITE_DIRECTLY(bool, bool)
              case READ_INT8:    WRITE_DIRECTLY(int8_t, int8)
              case READ_INT16:   WRITE_DIRECTLY(int16_t, int16)
              case READ_INT32:   WRITE_DIRECTLY(int32_t, int32)
              case READ_INT64:   WRITE_DIRECTLY(int64_t, int64)
              case READ_INTP:    WRITE_DIRECTLY(ssize_t, intp)
              case READ_UINT8:   WRITE_DIRECTLY(uint8_t, uint8)
              case READ_UINT16:  WRITE_DIRECTLY(uint16_t, uint16)
              case READ_UINT32:  WRITE_DIRECTLY(uint32_t, uint32)
              case READ_UINT64:  WRITE_DIRECTLY(uint64_t, uint64)
              case READ_UINTP:   WRITE_DIRECTLY(size_t, uintp)
              case READ_FLOAT32: WRITE_DIRECTLY(float, float32)
              case READ_FLOAT64: WRITE_DIRECTLY(double, float64)
            }

            count_writes_++;

          } // end if READ_DIRECT

          else {
              # define WRITE_TO_STACK(TYPE) {                                  \
                TYPE* ptr = reinterpret_cast<TYPE*>(                           \
                    current_inputs_[(IndexTypeOf<int64_t>)in_num].get()->read( \
                        num_items * (int64_t)sizeof(TYPE), current_error_));   \
                if (current_error_ != util::ForthError::none) {                \
                  return;                                                      \
                }                                                              \
                for (int64_t i = 0;  i < num_items;  i++) {                    \
                  TYPE value = ptr[i];                                         \
                  if (stack_cannot_push()) {                                   \
                    current_error_ = util::ForthError::stack_overflow;         \
                    return;                                                    \
                  }                                                            \
                  stack_push(value);                                           \
                }                                                              \
                break;                                                         \
              }

              # define WRITE_TO_STACK_SWAP(TYPE, SWAP) {                       \
                TYPE* ptr = reinterpret_cast<TYPE*>(                           \
                    current_inputs_[(IndexTypeOf<int64_t>)in_num].get()->read( \
                        num_items * (int64_t)sizeof(TYPE), current_error_));   \
                if (current_error_ != util::ForthError::none) {                \
                  return;                                                      \
                }                                                              \
                for (int64_t i = 0;  i < num_items;  i++) {                    \
                  TYPE value = ptr[i];                                         \
                  if (byteswap) {                                              \
                    SWAP(1, &value);                                           \
                  }                                                            \
                  if (stack_cannot_push()) {                                   \
                    current_error_ = util::ForthError::stack_overflow;         \
                    return;                                                    \
                  }                                                            \
                  stack_push((I)value);                                        \
                }                                                              \
                break;                                                         \
              }

              # define WRITE_TO_STACK_SWAP_INTP(TYPE) {                        \
                TYPE* ptr = reinterpret_cast<TYPE*>(                           \
                    current_inputs_[(IndexTypeOf<int64_t>)in_num].get()->read( \
                        num_items * (int64_t)sizeof(TYPE), current_error_));   \
                if (current_error_ != util::ForthError::none) {                \
                  return;                                                      \
                }                                                              \
                for (int64_t i = 0;  i < num_items;  i++) {                    \
                  TYPE value = ptr[i];                                         \
                  if (byteswap) {                                              \
                    if (sizeof(ssize_t) == 4) {                                \
                      byteswap32(1, &value);                                   \
                    }                                                          \
                    else {                                                     \
                      byteswap64(1, &value);                                   \
                    }                                                          \
                  }                                                            \
                  if (stack_cannot_push()) {                                   \
                    current_error_ = util::ForthError::stack_overflow;         \
                    return;                                                    \
                  }                                                            \
                  stack_push((I)value);                                        \
                }                                                              \
                break;                                                         \
              }

            switch (~bytecode & READ_MASK) {
              case READ_BOOL:    WRITE_TO_STACK(bool)
              case READ_INT8:    WRITE_TO_STACK(int8_t)
              case READ_INT16:   WRITE_TO_STACK_SWAP(int16_t, byteswap16)
              case READ_INT32:   WRITE_TO_STACK_SWAP(int32_t, byteswap32)
              case READ_INT64:   WRITE_TO_STACK_SWAP(int64_t, byteswap64)
              case READ_INTP:    WRITE_TO_STACK_SWAP_INTP(ssize_t)
              case READ_UINT8:   WRITE_TO_STACK(uint8_t)
              case READ_UINT16:  WRITE_TO_STACK_SWAP(uint16_t, byteswap16)
              case READ_UINT32:  WRITE_TO_STACK_SWAP(uint32_t, byteswap32)
              case READ_UINT64:  WRITE_TO_STACK_SWAP(uint64_t, byteswap64)
              case READ_UINTP:   WRITE_TO_STACK_SWAP_INTP(size_t)
              case READ_FLOAT32: WRITE_TO_STACK_SWAP(float, byteswap32)
              case READ_FLOAT64: WRITE_TO_STACK_SWAP(double, byteswap64)
            }

          } // end if not READ_DIRECT (i.e. read to stack)

          count_reads_++;

        } // end if bytecode < 0

        else if (bytecode >= BOUND_DICTIONARY) {
          if (recursion_current_depth_ == recursion_max_depth_) {
            current_error_ = util::ForthError::recursion_depth_exceeded;
            return;
          }
          bytecodes_pointer_push(bytecode - BOUND_DICTIONARY);
        }

        else {
          switch (bytecode) {
            case CODE_LITERAL: {
              I num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push((T)num);
              break;
            }

            case CODE_HALT: {
              is_ready_ = false;
              recursion_current_depth_ = 0;
              while (recursion_target_depth_.size() > 1) {
                recursion_target_depth_.pop();
              }
              do_current_depth_ = 0;
              current_error_ = util::ForthError::user_halt;

              // HALT counts as an instruction.
              count_instructions_++;
              return;
            }

            case CODE_PAUSE: {
              // In case of 'do ... pause loop/+loop', update the do-stack.
              if (is_segment_done()) {
                bytecodes_pointer_pop();

                if (do_current_depth_ != 0  &&
                    do_abs_recursion_depth() == recursion_current_depth_) {
                  // End one step of a 'do ... loop' or a 'do ... +loop'.
                  if (do_loop_is_step()) {
                    if (stack_cannot_pop()) {
                      current_error_ = util::ForthError::stack_underflow;
                      return;
                    }
                    do_i() += stack_pop();
                  }
                  else {
                    do_i()++;
                  }
                }
              }

              // PAUSE counts as an instruction.
              count_instructions_++;
              return;
            }

            case CODE_IF: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              if (stack_pop() == 0) {
                // Predicate is false, so skip over the next instruction.
                bytecodes_pointer_where()++;
              }
              break;
            }

            case CODE_IF_ELSE: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              if (stack_pop() == 0) {
                // Predicate is false, so skip over the next instruction
                // but do the one after that.
                bytecodes_pointer_where()++;
              }
              else {
                // Predicate is true, so do the next instruction (we know it's
                // in the dictionary), but skip the one after that.
                I consequent = bytecode_get();
                bytecodes_pointer_where() += 2;
                if (recursion_current_depth_ == recursion_max_depth_) {
                  current_error_ = util::ForthError::recursion_depth_exceeded;
                  return;
                }
                bytecodes_pointer_push(consequent - BOUND_DICTIONARY);

                // Ordinarily, a redirection like the above would count as one.
                count_instructions_++;
              }
              break;
            }

            case CODE_DO: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2();
              if (do_current_depth_ == recursion_max_depth_) {
                current_error_ = util::ForthError::recursion_depth_exceeded;
                return;
              }
              do_loop_push(pair[1], pair[0]);

              break;
            }

            case CODE_DO_STEP: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2();
              if (do_current_depth_ == recursion_max_depth_) {
                current_error_ = util::ForthError::recursion_depth_exceeded;
                return;
              }
              do_steploop_push(pair[1], pair[0]);
              break;
            }

            case CODE_AGAIN: {
              // Go back and do the body again.
              bytecodes_pointer_where() -= 2;
              break;
            }

            case CODE_UNTIL: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              if (stack_pop() == 0) {
                // Predicate is false, so go back and do the body again.
                bytecodes_pointer_where() -= 2;
              }
              break;
            }

            case CODE_WHILE: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              if (stack_pop() == 0) {
                // Predicate is false, so skip over the conditional body.
                bytecodes_pointer_where()++;
              }
              else {
                // Predicate is true, so do the next instruction (we know it's
                // in the dictionary), but skip back after that.
                I posttest = bytecode_get();
                bytecodes_pointer_where() -= 2;
                if (recursion_current_depth_ == recursion_max_depth_) {
                  current_error_ = util::ForthError::recursion_depth_exceeded;
                  return;
                }
                bytecodes_pointer_push(posttest - BOUND_DICTIONARY);

                // Ordinarily, a redirection like the above would count as one.
                count_instructions_++;
              }
              break;
            }

            case CODE_EXIT: {
              I exitdepth = bytecode_get();
              bytecodes_pointer_where()++;
              recursion_current_depth_ -= exitdepth;
              while (do_current_depth_ != 0  &&
                     do_abs_recursion_depth() != recursion_current_depth_) {
                do_current_depth_--;
              }

              count_instructions_++;
              if (single_step) {
                if (is_segment_done()) {
                  bytecodes_pointer_pop();
                }
                return;
              }

              // StackOverflow said I could: https://stackoverflow.com/a/1257776/1623645
              //
              // (I need to 'break' out of a loop, but we're in a switch statement,
              // so 'break' won't apply to the looping structure. I think this is the
              // first 'goto' I've written since I was writing in BASIC (c. 1985).
              goto after_end_of_segment;
            }

            case CODE_PUT: {
              I num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T value = stack_pop();
              variables_[(IndexTypeOf<T>)num] = value;
              break;
            }

            case CODE_INC: {
              I num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T value = stack_pop();
              variables_[(IndexTypeOf<T>)num] += value;
              break;
            }

            case CODE_GET: {
              I num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push(variables_[(IndexTypeOf<T>)num]);
              break;
            }

            case CODE_LEN_INPUT: {
              I in_num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push((I)current_inputs_[(IndexTypeOf<int64_t>)in_num].get()->len());
              break;
            }

            case CODE_POS: {
              I in_num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push((I)current_inputs_[(IndexTypeOf<int64_t>)in_num].get()->pos());
              break;
            }

            case CODE_END: {
              I in_num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push(current_inputs_[(IndexTypeOf<int64_t>)in_num].get()->end() ? -1 : 0);
              break;
            }

            case CODE_SEEK: {
              I in_num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              current_inputs_[(IndexTypeOf<int64_t>)in_num].get()->seek(stack_pop(), current_error_);
              if (current_error_ != util::ForthError::none) {
                return;
              }
              break;
            }

            case CODE_SKIP: {
              I in_num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              current_inputs_[(IndexTypeOf<int64_t>)in_num].get()->skip(stack_pop(), current_error_);
              if (current_error_ != util::ForthError::none) {
                return;
              }
              break;
            }

            case CODE_WRITE: {
              I out_num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* top = stack_peek();
              write_from_stack(out_num, top);
              stack_depth_--;

              count_writes_++;
              break;
            }

            case CODE_LEN_OUTPUT: {
              I out_num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push((I)current_outputs_[(IndexTypeOf<int64_t>)out_num].get()->len());
              break;
            }

            case CODE_REWIND: {
              I out_num = bytecode_get();
              bytecodes_pointer_where()++;
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              current_outputs_[(IndexTypeOf<int64_t>)out_num].get()->rewind(stack_pop(), current_error_);
              if (current_error_ != util::ForthError::none) {
                return;
              }
              break;
            }

            case CODE_I: {
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push((I)do_i());
              break;
            }

            case CODE_J: {
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push((I)do_j());
              break;
            }

            case CODE_K: {
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push((I)do_k());
              break;
            }

            case CODE_DUP: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_buffer_[stack_depth_] = stack_buffer_[stack_depth_ - 1];
              stack_depth_++;
              break;
            }

            case CODE_DROP: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              stack_depth_--;
              break;
            }

            case CODE_SWAP: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              int64_t tmp = stack_buffer_[stack_depth_ - 2];
              stack_buffer_[stack_depth_ - 2] = stack_buffer_[stack_depth_ - 1];
              stack_buffer_[stack_depth_ - 1] = (T)tmp;
              break;
            }

            case CODE_OVER: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push(stack_buffer_[stack_depth_ - 2]);
              break;
            }

            case CODE_ROT: {
              if (stack_cannot_pop3()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              int64_t tmp1 = stack_buffer_[stack_depth_ - 3];
              stack_buffer_[stack_depth_ - 3] = stack_buffer_[stack_depth_ - 2];
              stack_buffer_[stack_depth_ - 2] = stack_buffer_[stack_depth_ - 1];
              stack_buffer_[stack_depth_ - 1] = (T)tmp1;
              break;
            }

            case CODE_NIP: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              stack_buffer_[stack_depth_ - 2] = stack_buffer_[stack_depth_ - 1];
              stack_depth_--;
              break;
            }

            case CODE_TUCK: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              int64_t tmp = stack_buffer_[stack_depth_ - 1];
              stack_buffer_[stack_depth_ - 1] = stack_buffer_[stack_depth_ - 2];
              stack_buffer_[stack_depth_ - 2] = (T)tmp;
              stack_push((T)tmp);
              break;
            }

            case CODE_ADD: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] + pair[1];
              break;
            }

            case CODE_SUB: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] - pair[1];
              break;
            }

            case CODE_MUL: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] * pair[1];
              break;
            }

            case CODE_DIV: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              if (pair[1] == 0) {
                current_error_ = util::ForthError::division_by_zero;
                return;
              }
              // Forth (gforth, at least) does floor division; C++ does integer division.
              // This makes a difference for negative numerator or denominator.
              T tmp = pair[0] / pair[1];
              pair[0] = tmp * pair[1] == pair[0] ? tmp : tmp - ((pair[0] < 0) ^ (pair[1] < 0));
              break;
            }

            case CODE_MOD: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              if (pair[1] == 0) {
                current_error_ = util::ForthError::division_by_zero;
                return;
              }
              // Forth (gforth, at least) does modulo; C++ does remainder.
              // This makes a difference for negative numerator or denominator.
              pair[0] = (pair[1] + (pair[0] % pair[1])) % pair[1];
              break;
            }

            case CODE_DIVMOD: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T one = stack_buffer_[stack_depth_ - 2];
              T two = stack_buffer_[stack_depth_ - 1];
              if (two == 0) {
                current_error_ = util::ForthError::division_by_zero;
                return;
              }
              // See notes on division and modulo/remainder above.
              T tmp = one / two;
              stack_buffer_[stack_depth_ - 1] =
                  tmp * two == one ? tmp : tmp - ((one < 0) ^ (two < 0));
              stack_buffer_[stack_depth_ - 2] =
                  (two + (one % two)) % two;
              break;
            }

            case CODE_NEGATE: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* top = stack_peek();
              *top = -(*top);
              break;
            }

            case CODE_ADD1: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* top = stack_peek();
              (*top)++;
              break;
            }

            case CODE_SUB1: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* top = stack_peek();
              (*top)--;
              break;
            }

            case CODE_ABS: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* top = stack_peek();
              *top = abs(*top);
              break;
            }

            case CODE_MIN: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = std::min(pair[0], pair[1]);
              break;
            }

            case CODE_MAX: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = std::max(pair[0], pair[1]);
              break;
            }

            case CODE_EQ: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] == pair[1] ? -1 : 0;
              break;
            }

            case CODE_NE: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] != pair[1] ? -1 : 0;
              break;
            }

            case CODE_GT: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] > pair[1] ? -1 : 0;
              break;
            }

            case CODE_GE: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] >= pair[1] ? -1 : 0;
              break;
            }

            case CODE_LT: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] < pair[1] ? -1 : 0;
              break;
            }

            case CODE_LE: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] <= pair[1] ? -1 : 0;
              break;
            }

            case CODE_EQ0: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* top = stack_peek();
              *top = *top == 0 ? -1 : 0;
              break;
            }

            case CODE_INVERT: {
              if (stack_cannot_pop()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* top = stack_peek();
              *top = ~(*top);
              break;
            }

            case CODE_AND: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] & pair[1];
              break;
            }

            case CODE_OR: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] | pair[1];
              break;
            }

            case CODE_XOR: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] ^ pair[1];
              break;
            }

            case CODE_LSHIFT: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] << pair[1];
              break;
            }

            case CODE_RSHIFT: {
              if (stack_cannot_pop2()) {
                current_error_ = util::ForthError::stack_underflow;
                return;
              }
              T* pair = stack_pop2_before_pushing1();
              pair[0] = pair[0] >> pair[1];
              break;
            }

            case CODE_FALSE: {
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push(0);
              break;
            }

            case CODE_TRUE: {
              if (stack_cannot_push()) {
                current_error_ = util::ForthError::stack_overflow;
                return;
              }
              stack_push(-1);
              break;
            }
          }
        } // end handle one instruction

        count_instructions_++;
        if (single_step) {
          if (is_segment_done()) {
            bytecodes_pointer_pop();
          }
          return;
        }

      } // end walk over instructions in this segment

    after_end_of_segment:
      bytecodes_pointer_pop();

      if (do_current_depth_ != 0  &&
          do_abs_recursion_depth() == recursion_current_depth_) {
        // End one step of a 'do ... loop' or a 'do ... +loop'.
        if (do_loop_is_step()) {
          if (stack_cannot_pop()) {
            current_error_ = util::ForthError::stack_underflow;
            return;
          }
          do_i() += stack_pop();
        }
        else {
          do_i()++;
        }
      }

    } // end of all segments
  }

  template <>
  void
  ForthMachineOf<int32_t, int32_t>::write_from_stack(int64_t num, int32_t* top) noexcept {
    if (num == 1) {
      current_outputs_[(IndexTypeOf<int64_t>)num].get()->write_one_int32(*top, false);
    }
    else {
      current_outputs_[(IndexTypeOf<int64_t>)num].get()->write_int32(1, top, false);
    }
  }

  template <>
  void
  ForthMachineOf<int64_t, int32_t>::write_from_stack(int64_t num, int64_t* top) noexcept {
    if (num == 1) {
      current_outputs_[(IndexTypeOf<int64_t>)num].get()->write_one_int64(*top, false);
    }
    else {
      current_outputs_[(IndexTypeOf<int64_t>)num].get()->write_int64(1, top, false);
    }
  }

  template class EXPORT_TEMPLATE_INST ForthMachineOf<int32_t, int32_t>;
  template class EXPORT_TEMPLATE_INST ForthMachineOf<int64_t, int32_t>;

}