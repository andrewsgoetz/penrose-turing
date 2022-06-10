#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <argp.h>

// Configuration for argp.
const char* argp_program_version = "0.1.0";
static char doc[] =
"\
Execute a Penrose-style Turing machine as described in \"The Emperor's New \
Mind\". The Turing machine specification (using Penrose's encoding) and the \
initial tape can be specified via the command line or file, with the file \
option taking precedence.\n\
\n\
If the tape is not specified, the Turing machine specification will be \
printed in the format used by Penrose, except that state numbers will be \
printed in hexadecimal instead of binary.\n\
\n\
If the tape is specified then the verbosity level controls the output. \
";
static struct argp_option options[] = {
  {"tm",                 'm', "TM",                  0,  "Turing machine specification TM" },
  {"tm-file",           1000, "FILE",                0,  "read Turing machine specification from FILE" },
  {"tape",               't', "TAPE",                0,  "initial tape TAPE" },
  {"tape-file",         1001, "FILE",                0,  "read initial tape from FILE" },
  {"max-tape-length",   1002, "N",                   0,  "stop if number of cells in working tape exceeds N (default: 2^20)" },
  {"max-steps",         1003, "N",                   0,  "stop if number of Turing machine steps exceeds N (default: 2^20)" },
  {"verbosity",          'v', "N", OPTION_ARG_OPTIONAL,  "verbosity (0-2), e.g. -v -v or -v2 for level 2" },
  //{"quiet",    'q', 0,      0,  "Don't produce any output" },
  //{"silent",   's', 0,      OPTION_ALIAS },
  //{"output",   'o', "FILE", 0, "Output to FILE instead of standard output" },
  { 0 }
};
struct arguments {
  const char* tm;
  const char* tm_file;
  const char* tape;
  const char* tape_file;
  const char* max_tape_len_str;
  const char* max_steps_str;
  int verbosity;
};
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
  struct arguments *args = state->input;

  switch (key) {
    case 'm':
      args->tm = arg;
      break;
    case 't':
      args->tape = arg;
      break;
    case 1000:
      args->tm_file = arg;
      break;
    case 1001:
      args->tape_file = arg;
      break;
    case 1002:
      args->max_tape_len_str = arg;
      break;
    case 1003:
      args->max_steps_str = arg;
      break;
    case 'v':
      if (arg) {
        args->verbosity = atoi(arg);
      } else {
        ++args->verbosity;
      }
      break;
    case ARGP_KEY_ARG:
      argp_usage(state);
      break;
    case ARGP_KEY_SUCCESS:
      if ((!args->tm && !args->tm_file)) {
        argp_usage(state);
      }
    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}
static struct argp argp = { options, parse_opt, 0, doc };

// The possible tokens in the Turing machine encoding.
// token | encoding
// ----- | --------
//     0 | 0
//     1 | 10
//     R | 110
//     L | 1110
//  STOP | 11110
enum token { ZERO, ONE, RIGHT, LEFT, STOP };

// Data structure defining a single Turing machine state.
struct action;
struct state;
struct action {
  short value_to_write; // 0 or 1
  short direction_to_move; // -1 or +1, 0 for STOP
  struct state* next_state;
};
struct state {
  size_t number;
  struct action action0; // action to take after reading '0'
  struct action action1; // action to take after reading '1'
};

// Helper function signatures.
void read_text_file(const char* f, const char** buffer);
void parse_tm(const char* tm, struct state** states, size_t* states_len);
void print_tm(const struct state* states, size_t states_len);
void print_tm_action(size_t state_number, char c, const struct action* action);
void run(const struct state* states, const char* initial_tape, size_t max_tape_len, unsigned long long max_steps, int verbosity);
void print_tape(const char* tape, int tape_len, int tape_ix, unsigned long long step, const struct state* state);

// Default constants.
static const char* const DEFAULT_MAX_TAPE_LEN = "1048576"; // 2^20
static const char* const DEFAULT_MAX_STEPS = "1048576"; // 2^20

int main(const int argc, char *argv[]) {
  struct arguments args = {0};
  args.max_tape_len_str = DEFAULT_MAX_TAPE_LEN;
  args.max_steps_str = DEFAULT_MAX_STEPS;
  argp_parse(&argp, argc, argv, 0, 0, &args);

  if (args.tm_file) {
    read_text_file(args.tm_file, &(args.tm));
  }
  if (args.tape_file) {
    read_text_file(args.tape_file, &(args.tape));
  }

  struct state* states;
  size_t states_len;
  parse_tm(args.tm, &states, &states_len);

  // If there is no tape, just print the Turing machine specification.
  if (args.tape == NULL) {
    print_tm(states, states_len);
    return 0;
  }

  const size_t max_tape_len = (size_t) strtoull(args.max_tape_len_str, NULL, 10);
  if (max_tape_len == 0) {
    fprintf(stderr, "Maximum tape length must be a positive integer; was %s.\n", args.max_tape_len_str);
    exit(1);
  }
  const unsigned long long max_steps = strtoull(args.max_steps_str, NULL, 10);
  if (max_steps == 0) {
    fprintf(stderr, "Maximum number of steps must be a positive integer; was %s.\n", args.max_steps_str);
    exit(1);
  }

  run(states, args.tape, max_tape_len, max_steps, args.verbosity);

  return 0;
}

/**
 * Reads a text file.
 *
 * Parameters
 * ----------
 * f - file name
 *
 * "Out" Parameters
 * ----------------
 * buffer - buffer
 */
void read_text_file(const char* const f, const char** buffer) {
  FILE* const fp = fopen(f, "r");
  if (fp == NULL) {
    printf("Error opening file %s.", f);
    exit(1);
  }
  char* tmp1 = NULL;
  size_t tmp2;
  const ssize_t buffer_len = getdelim(&tmp1, &tmp2, '\0', fp);
  if (buffer_len == -1) {
    printf("Error reading file %s.", f);
    exit(1);
  }
  *buffer = tmp1;
  fclose(fp);
}

/**
 * Parses the Turing machine specification.
 *
 * Parameters
 * ----------
 * tm     - Turing machine specification
 *
 * "Out" Parameters
 * ----------------
 * states     - pointer to the array of states (memory allocated by this function)
 * states_len - length of array of states
 */
void parse_tm(const char* const tm, struct state** states, size_t* states_len) {
  // Add implicit "110" at beginning and end of specification.
  // Also check that the specification consists of only '0's and '1's.
  size_t tm_len = 0;
  for (const char* s = tm; *s != '\0'; ++s) {
    if (*s != '0' && *s != '1') {
      fprintf(stderr, "Invalid Turing machine specification at index %zu; "
                      "encoding must consist of 0s and 1s only.\n", tm_len);
      exit(1);
    }
    ++tm_len;
  }
  tm_len += 6;
  char* const tm_tmp = (char *) calloc(tm_len + 1, sizeof(char));
  if (tm_tmp == NULL) {
    fputs("Out of memory.\n", stderr);
    exit(1);
  }
  tm_tmp[0] = '1';
  tm_tmp[1] = '1';
  tm_tmp[2] = '0';
  for (size_t i = 3; i < tm_len - 3; ++i) {
    tm_tmp[i] = tm[i - 3];
  }
  tm_tmp[tm_len - 3] = '1';
  tm_tmp[tm_len - 2] = '1';
  tm_tmp[tm_len - 1] = '0';

  // Tokenize.
  size_t tokens_len = 0;
  for (size_t i = 0; i < tm_len; ++i) {
    if (tm_tmp[i] == '0') {
      ++tokens_len;
    }
  }
  enum token* const tokens = (enum token *) calloc(tokens_len, sizeof(enum token));
  if (tokens == NULL) {
    fputs("Out of memory.\n", stderr);
    exit(1);
  }
  size_t token_ix = 0;
  short token_len = 0;
  for (size_t i = 0; i < tm_len; ++i) {
    ++token_len;
    if (token_len > 5) {
      fprintf(stderr, "Invalid Turing machine specification at index %zu; "
                      "specification contains more than four '1's.\n", i);
      exit(1);
    }
    if (tm_tmp[i] == '0') {
      tokens[token_ix++] = (enum token) (token_len - 1);
      token_len = 0;
    }
  }
  free(tm_tmp);

  // Count the number of states.
  size_t actions_len = 0;
  for (size_t i = 0; i < tokens_len; ++i) {
    if (tokens[i] >= RIGHT) { // Increment if the current token is RIGHT, LEFT, or STOP.
      ++actions_len;
    }
  }
  if (actions_len % 2 != 0) {
    fputs("Invalid Turing machine specification; "
          "every state must define what to do after reading either a '0' or a '1'.",
          stderr);
    exit(1);
  }
  *states_len = actions_len / 2;

  // Create the Turing machine states.
  *states = (struct state *) calloc(*states_len, sizeof(struct state));
  size_t action_ix = 0;
  size_t state_ix = 0;
  enum token* token_start = tokens;
  for (size_t i = 0; i < tokens_len; ++i) {
    enum token token = tokens[i];
    if (token < RIGHT) { // Continue until reaching RIGHT, LEFT, or STOP.
      continue;
    }

    struct state* state = *states + state_ix;
    state->number = state_ix;
    struct action* action = action_ix % 2 == 0 ? &(state->action0) : &(state->action1);
    if (token == RIGHT) {
      action->direction_to_move = +1;
    } else if (token == LEFT) {
      action->direction_to_move = -1;
    } else { // STOP
      action->direction_to_move = 0;
    }

    if (token_start == tokens + i) {
      action->value_to_write = 0;
      action->next_state = *states; // 0th state
    } else if (token_start == tokens + i - 1) {
      action->value_to_write = tokens[i - 1];
      action->next_state = *states; // 0th state
    } else {
      action->value_to_write = tokens[i - 1];

      // Read index of next state from binary value.
      size_t next_state_ix = 0;
      int j = 0;
      for (enum token* t = tokens + i - 2; t >= token_start; --t) {
        next_state_ix += ((unsigned int) (*t)) * 1u << j++;
      }
      if (next_state_ix > *states_len - 1) {
        fprintf(stderr, "Invalid Turing machine specification; "
                        "state %zX has a transition to non-existent state %zX.", state->number, next_state_ix);
        exit(1);
      }
      action->next_state = *states + next_state_ix;
    }

    // Update for next iteration.
    ++action_ix;
    if (action_ix % 2 == 0) {
      ++state_ix;
    }
    token_start = tokens + i + 1;
  }
  free(tokens);
}

/**
 * Prints a Turing machine specification.
 *
 * Parameters
 * ----------
 * states     - array of states
 * states_len - length of array of states
 */
void print_tm(const struct state* const states, const size_t states_len) {
  for (size_t i = 0; i < states_len; ++i) {
    const struct state* const s = states + i;
    print_tm_action(s->number, '0', &(s->action0));
    print_tm_action(s->number, '1', &(s->action1));
  }
}

/**
 * Prints a Turing machine state action.
 *
 * Parameters
 * ----------
 * state_number - state number
 * c            - either '0' or '1', the character which when read triggers the action
 * action       - action
 */
void print_tm_action(const size_t state_number, const char c, const struct action* const action) {
  char* direction;
  if (action->direction_to_move == -1) {
    direction = "L";
  } else if (action->direction_to_move == +1) {
    direction = "R";
  } else {
    direction = "STOP";
  }

  fprintf(stdout, "%5zX %c -> %5zX %d %s\n", state_number, c, action->next_state->number, action->value_to_write, direction);
}

/**
 * Runs a Turing machine.
 *
 * Parameters
 * ----------
 * states       - Turing machine states
 * initial_tape - initial tape
 * max_tape_len - maximum tape length allowed
 * max_steps    - maximum number of steps allowed
 * verbosity    - verbosity level between 0 and 2
 */
void run(const struct state* const states, const char* const initial_tape,
         const size_t max_tape_len, const unsigned long long max_steps,
         const int verbosity) {
  size_t initial_tape_len = 0;
  for (const char* s = initial_tape; *s != '\0'; ++s) {
    if (*s != '0' && *s != '1') {
      fprintf(stderr, "Invalid tape at index %zu; "
                      "must consist of 0s and 1s only.\n", initial_tape_len);
      exit(1);
    }
    ++initial_tape_len;
  }
  char* tape = (char *) calloc(initial_tape_len + 1, sizeof(char));
  strcpy(tape, initial_tape);
  size_t tape_len = initial_tape_len;

  // First execution figures out how much tape is used.
  size_t tape_expansion_amt = 1024;
  const struct state* curr_state = states; // start in state zero
  ssize_t tape_ix = 0;
  ssize_t rel_tape_ix = 0;
  ssize_t min_rel_tape_ix = 0;
  ssize_t max_rel_tape_ix = tape_len - 1;
  unsigned long long step = 0;
  while(1) {
    if (step == max_steps) {
      fprintf(stderr, "Exceeded maximum number of steps (%llu).\n", max_steps);
      exit(1);
    }
    ++step;
    if (tape_len > max_tape_len) {
      fprintf(stderr, "Exceeded maximum length of working tape (%zu).\n", max_tape_len);
      exit(1);
    }
    struct action action;
    if (tape[tape_ix] == '0' || tape[tape_ix] == ' ') {
      action = curr_state->action0;
    } else {
      action = curr_state->action1;
    }
    tape[tape_ix] = action.value_to_write == 0 ? '0' : '1';
    if (action.direction_to_move == 0) {
      break;
    }
    tape_ix += action.direction_to_move;
    rel_tape_ix += action.direction_to_move;
    if (rel_tape_ix < min_rel_tape_ix) {
      min_rel_tape_ix = rel_tape_ix;
    }
    if (rel_tape_ix > max_rel_tape_ix) {
      max_rel_tape_ix = rel_tape_ix;
    }
    if (tape_ix < 0 || tape_ix == tape_len) { // Expand the tape.
      char* const tape_tmp = (char *) calloc(tape_len + tape_expansion_amt + 1, sizeof(char));
      if (tape_ix < 0) {
        for (size_t i = 0; i < tape_expansion_amt; ++i) {
          tape_tmp[i] = ' '; // last blank will be overwritten in next iteration
        }
        strcpy(tape_tmp + tape_expansion_amt, tape);
        tape_ix += tape_expansion_amt;
      } else {
        strcpy(tape_tmp, tape);
        for (size_t i = 0; i < tape_expansion_amt; ++i) {
          tape_tmp[tape_len + i] = ' '; // first blank will be overwritten in next iteration
        }
        tape_tmp[tape_len + tape_expansion_amt] = '\0';
      }
      tape_len += tape_expansion_amt;
      tape_expansion_amt *= 2;
      free(tape);
      tape = tape_tmp;
    }
    curr_state = action.next_state;
  }

  if (verbosity == 0) {
    tape[tape_ix + 1] = '\0';
    const char* s = tape + tape_ix;
    while (*s != ' ' && s >= tape) {
      --s;
    }
    fputs(++s, stdout);
    putchar('\n');
    return;
  }

  // Second execution if we need verbosity.
  size_t final_tape_len = max_rel_tape_ix - min_rel_tape_ix + 1;
  tape_ix = -min_rel_tape_ix;
  free(tape);
  tape = (char *) calloc(final_tape_len + 1, sizeof(char));
  for (size_t i = 0; i < final_tape_len; ++i) {
    tape[i] = ' ';
  }
  for (size_t i = 0; i < initial_tape_len; ++i) {
    tape[tape_ix + i] = initial_tape[i];
  }
  tape[final_tape_len] = '\0';

  step = 0;
  curr_state = states;
  print_tape(tape, final_tape_len, tape_ix, step, curr_state);
  while(1) {
    ++step;
    char curr_value = tape[tape_ix];
    struct action action;
    if (curr_value == '0' || curr_value == ' ') {
      action = curr_state->action0;
    } else {
      action = curr_state->action1;
    }
    char value_to_write = action.value_to_write == 0 ? '0' : '1';
    tape[tape_ix] = value_to_write;
    if (verbosity == 2 || value_to_write != curr_value) {
      print_tape(tape, final_tape_len, tape_ix, step, curr_state);
    }
    if (action.direction_to_move == 0) {
      break;
    }
    tape_ix += action.direction_to_move;
    curr_state = action.next_state;
  }
}

/**
 * Prints the contents of the tape to stdout.
 *
 * Parameters
 * ----------
 * tape     - tape string of ' 's, '0's, and '1's
 * tape_len - tape string length
 * tape_ix  - index in tape string of current cell
 * step     - step number of Turing machine operation
 * state    - pointer to current state of Turing machine
 */
void print_tape(const char* const tape, const int tape_len, int tape_ix,
                unsigned long long step, const struct state* const state) {
  if (step >= 0) {
    printf("%5llu ", step);
  } else {
    printf("      ");
  }
  if (state != NULL) {
    printf("%5zX:", state->number);
  } else {
    printf("   :");
  }
  for (int i = 0; i < tape_ix; ++i) {
    putchar(' ');
    putchar(tape[i]);
  }
  putchar(step > 0 ? '|' : ' ');
  putchar(tape[tape_ix]);
  putchar(step > 0 ? '|' : ' ');
  for (int i = tape_ix + 1; i < tape_len; ++i) {
    putchar(tape[i]);
    putchar(' ');
  }
  putchar('\n');
}
