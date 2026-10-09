/* cc1.h - shared declarations for cc1, the front end: a preprocessed C
 * source (.i) to IR (docs/ir_format.md), for the language of docs/c89_spec.md.
 *
 * Files: lex.c (input, tokens, names, diagnostics), type.c (types and
 * symbols), expr.c (expressions: parsing, typing, constant folding),
 * emit.c (IR output), stmt.c (declarations, statements, main).
 *
 * In the pipeline: the driver runs cpp, which writes the .i file cc1
 * reads (plain C plus "# line" markers, #asm blocks and #pragma weak);
 * cc1 writes one IR file for the translation unit, which cc2 turns into
 * eZ80 assembly. cc1 owns everything that is C: tokens, types, scopes,
 * diagnostics, constant folding, and the frame layout of locals. The IR
 * it hands on has no C types, only value kinds (I, L, F, D, Q) and sizes.
 *
 * The flow is a single pass of recursive descent: stmt.c's main reads a
 * declaration at a time; a function body is parsed statement by
 * statement, each expression into a small tree of nodes (struct enode)
 * that expr.c types and folds, emit.c writes out in postfix order, and
 * the statement then discards. Nothing of a function is kept once its
 * IR is written, except the symbols and types it declared.
 *
 * Every table is a fixed-size array indexed by small ints: a type, a
 * symbol, a name or a node is its index, never a pointer. That keeps the
 * compiler's memory use fixed and visible (the limits below), which
 * matters when it runs on the Agon itself, and lets types be compared
 * with ==. An index of -1 usually means "none".
 */

#ifndef CC1_H
#define CC1_H

#include <stdio.h>
#include "int32.h"
#include "softfp.h"
#include "int64.h"

/* ---- limits (fixed tables; -v reports peaks) ------------------------------ */

/* Running out of one is a fatal error naming the table, never a silent
 * truncation. MAX_IDENT is c89_spec.md 4's 47 significant characters. */
#define MAX_IDENT 47
#define NAME_TEXT 20000         /* interned identifier text */
#define MAX_NAMES 2000
#define NAME_HASH 4096
#define MAX_STRLIT 600          /* one string literal's bytes (509 + slack) */
#define MAX_TYPES 800
#define MAX_PLIST 1500          /* parameter types, all function types together */
#define MAX_GLOBALS 1200
#define MAX_LOCALS 300
#define MAX_BLOCKS 200          /* blocks (compound statements) in one function */
#define MAX_DEPTH 64            /* blocks nested in one function */
#define MAX_NODES 500           /* expression nodes live at once */
#define MAX_FUNC_STRS 400       /* string literals in one function */
#define MAX_ARGS_CALL 31
#define MAX_TAGS 150            /* struct and enum tags */
#define MAX_MEMBERS 800         /* struct members, all structs together */
#define MAX_CASES 300           /* case labels in one function (C89: 257 in one switch) */
#define MAX_SWITCHES 100        /* switch statements in one function */
#define MAX_LABELS 100          /* goto labels in one function */
#define MAX_IR_LABELS 1500      /* IR labels in one function (cc2's limit too) */

/* ---- tokens ------------------------------------------------------------------- */

/* A token kind is one int: the small fixed kinds, then the keywords from
 * TK_KW (keyword i is also interned name i, see lex_open), then single
 * character punctuators as TK_P plus the character, then the multi
 * character punctuators from 400, above every TK_P + byte. */
#define TK_EOF 0
#define TK_IDENT 1
#define TK_NUM 2
#define TK_STR 3
#define TK_ASMB 4        /* a #asm ... #endasm block: tok_str, tok_len */
/* keywords: TK_KW + index into kw_names (lex.c) */
#define TK_KW 10
#define KW_AUTO 10
#define KW_BREAK 11
#define KW_CASE 12
#define KW_CHAR 13
#define KW_CONST 14
#define KW_CONTINUE 15
#define KW_DEFAULT 16
#define KW_DO 17
#define KW_DOUBLE 18
#define KW_ELSE 19
#define KW_ENUM 20
#define KW_EXTERN 21
#define KW_FLOAT 22
#define KW_FOR 23
#define KW_GOTO 24
#define KW_IF 25
#define KW_INT 26
#define KW_LONG 27
#define KW_REGISTER 28
#define KW_RETURN 29
#define KW_SHORT 30
#define KW_SIGNED 31
#define KW_SIZEOF 32
#define KW_STATIC 33
#define KW_STRUCT 34
#define KW_SWITCH 35
#define KW_TYPEDEF 36
#define KW_UNION 37
#define KW_UNSIGNED 38
#define KW_VOID 39
#define KW_VOLATILE 40
#define KW_WHILE 41
#define KW_ASM 42
#define KW_INLINE 50      /* C99's inline, the default mode's (lex.c spots it: not a name-table keyword) */
#define KW_BOOL 51        /* C99's _Bool, likewise */
#define NKW 34            /* the last, __asm, is KW_ASM in both modes */
/* punctuators: single characters are TK_P + the character */
#define TK_P 100
#define P_ARROW 400
#define P_INC 401
#define P_DEC 402
#define P_SHL 403
#define P_SHR 404
#define P_LE 405
#define P_GE 406
#define P_EQ 407
#define P_NE 408
#define P_ANDAND 409
#define P_OROR 410
#define P_MULEQ 411
#define P_DIVEQ 412
#define P_MODEQ 413
#define P_ADDEQ 414
#define P_SUBEQ 415
#define P_SHLEQ 416
#define P_SHREQ 417
#define P_ANDEQ 418
#define P_XOREQ 419
#define P_OREQ 420
#define P_ELLIPSIS 421

/* ---- types ---------------------------------------------------------------------- */

/* A type is an index into types[] (type.c). Its kind is one of TY_*;
 * derived types (pointer, array, function, qualified) point at another
 * type by index. type.c never makes the same type twice, so two types
 * are the same exactly when their indices are equal. */
#define TY_VOID 1
#define TY_CHAR 2
#define TY_UCHAR 3
#define TY_INT 4
#define TY_UINT 5
#define TY_PTR 6
#define TY_ARRAY 7
#define TY_FUNC 8
#define TY_HOLE 9       /* placeholder while parsing a nested declarator */
#define TY_STRUCT 10    /* base: its tag; size 0 until the struct is complete */
#define TY_SCHAR 11     /* signed char: as char, but a distinct type */
#define TY_SHORT 12
#define TY_USHORT 13
#define TY_LONG 14
#define TY_ULONG 15
#define TY_FLOAT 16     /* IEEE binary32 */
#define TY_DOUBLE 17    /* IEEE binary64 */
#define TY_LDOUBLE 18   /* long double: a distinct type, double's representation */
#define TY_LLONG 19     /* long long (the default mode's, C99): 64 bits, handled by address */
#define TY_ULLONG 20
#define TY_BOOL 21      /* C99's _Bool: one byte, 0 or 1 (conversion to it is != 0) */

#define Q_CONST 1       /* type qualifiers (struct type.qual) */
#define Q_VOLATILE 2

/* The tables keep kinds, flags and small counts in bytes: every entry
 * costs its size in each of hundreds of slots. */
struct type {
    unsigned char kind;
    unsigned char qual;         /* Q_CONST | Q_VOLATILE; part of the type's identity */
    unsigned char variadic;     /* FUNC: the parameter list ends with "..." */
    signed char nparams;        /* FUNC: -1 for "()", a declaration without a prototype */
    int base;           /* PTR/ARRAY: element; FUNC: return type; STRUCT: tag */
    int count;          /* ARRAY: element count, -1 if unknown */
    int size;           /* bytes (read through type_size, which a struct needs) */
    int params;         /* FUNC: first index in plist */
};

/* the basic types, created first, so their numbers are fixed */
#define T_VOID 0
#define T_CHAR 1
#define T_UCHAR 2
#define T_INT 3
#define T_UINT 4
#define T_SCHAR 5
#define T_SHORT 6
#define T_USHORT 7
#define T_LONG 8
#define T_ULONG 9
#define T_FLOAT 10
#define T_DOUBLE 11
#define T_LDOUBLE 12
#define T_LLONG 13
#define T_ULLONG 14
#define T_BOOL 15

/* struct and enum tags: one namespace, file scope plus the current function.
 * A union is a struct tag with is_union set; its type is TY_STRUCT too. */
struct tag {
    int name;
    int fill;           /* while being defined: the bytes laid out so far (a union: the largest) */
    int type;           /* struct: its TY_STRUCT type; enum: T_INT */
    int members;        /* struct: first member, chained by member.next; -1 if none */
    int shadowed;       /* the tag this one hides (restored at the function's end), or -1 */
    unsigned char is_enum;
    unsigned char is_union;     /* a union: every member at offset 0, the size of the largest */
    unsigned char bits;         /* the bits used in the last byte by bit-fields (abi.md 2) */
    unsigned char complete;
    unsigned char local;        /* declared inside the current function: its scope depth */
    unsigned char flex;         /* a struct ending in a flexible array member (C99) */
};

/* one struct or union member; a tag's members form a list in declaration
 * order through next */
struct member {
    int name;
    int type;
    int offset;         /* bytes from the start of the struct */
    int next;           /* -1 ends the chain */
    unsigned char bit;          /* a bit-field: its first bit in the byte at offset */
    unsigned char width;        /* a bit-field's width; 0 for an ordinary member */
};

/* ---- symbols --------------------------------------------------------------------- */

/* Two tables: globals (file scope, kept for the whole unit, and every
 * object or function a block-scope extern or static names) and locals
 * (the current function's parameters, variables, temporaries and
 * block-scope names, cleared at its end). type.c maps each interned name
 * to its innermost symbol in each table; a declaration that hides an
 * outer one records it in shadowed, to be restored when its scope ends. */
#define SK_VAR 1
#define SK_FUNC 2
#define SK_TYPEDEF 3    /* type: the type it names */
#define SK_ENUMC 4      /* val: the enumerator's value */

#define SC_NONE 0
#define SC_EXTERN 1
#define SC_STATIC 2
#define SC_TYPEDEF 3
#define SC_AUTO 4          /* auto or register: only inside decl_specs */

struct gsym {
    int name;
    int type;
    int line;           /* where it was declared, for later diagnostics */
    int val;            /* SK_ENUMC: the value */
    unsigned char kind;         /* SK_* */
    unsigned char sclass;       /* SC_STATIC: internal linkage */
    unsigned char defined;      /* defined in this unit (enumerators and typedefs at once) */
    unsigned char used;         /* referred to (for the static function checks at the end) */
    unsigned char implicit;     /* SK_FUNC: declared only by a call (strict mode) */
    unsigned char tentative;    /* SK_VAR: a file-scope definition without an initialiser (C89 3.7.2) */
};

#define LK_VAR 0
#define LK_ENUMC 1      /* offset: the enumerator's value */
#define LK_GLOBAL 2     /* a block-scope extern or static: offset is the global symbol */
#define LK_TYPEDEF 3    /* a block-scope typedef: type is the type */

/* a local; a variable's or temporary's number (index) is the k of the
 * IR's "LA @k" and "LOC k" (a parameter is addressed by its offset) */
struct lsym {
    int name;           /* -1 for a temporary */
    int type;
    int offset;         /* parameter: ix+offset; LK_ENUMC, LK_GLOBAL: see above */
    int shadowed;       /* the local this name hid, or -1 */
    unsigned char kind;         /* LK_* */
    unsigned char param;        /* 1: parameter (offset known), 0: local */
    unsigned char temp;         /* 1: a temporary cc1 made (a call's struct result), placed after the rest */
    unsigned char depth;        /* the scope depth it was declared at (1: the function's outermost block) */
    unsigned char block;        /* the block it belongs to (0: the outermost), for the frame layout */
};

/* ---- expression nodes ----------------------------------------------------------- */

/* An expression is a tree of nodes in nodes[] (expr.c), linked by index
 * through a, b and c (-1: none); a unary node uses a. Each node carries
 * its C type. The table is reset after every statement, so MAX_NODES
 * bounds one statement's tree, not the function. */
#define EN_NUM 1        /* val */
#define EN_STR 2        /* val: function string number, or global symbol of a file-scope string */
#define EN_GVAR 3       /* val: gsym */
#define EN_LVAR 4       /* val: lsym */
/* EN_CALL: val: gsym, or -1 for a call through b, a function pointer;
 * a: the first EN_ARG (the last C argument first); c: the local holding
 * the result object of a struct, double or long long call */
#define EN_CALL 5
#define EN_DEREF 6      /* the object at address a */
#define EN_ADDR 7       /* the address of object a */
#define EN_BIN 8        /* val: B_* */
#define EN_NEG 9
#define EN_CPL 10
#define EN_NOT 11
#define EN_LAND 12
#define EN_LOR 13
#define EN_COND 14      /* a ? b : c */
#define EN_COMMA 15
#define EN_ASSIGN 16    /* the object a = b */
#define EN_ASGOP 17     /* val: B_*; a op= b, the address of a computed once */
#define EN_INC 18       /* val: 0 ++x, 1 x++, 2 --x, 3 x-- */
#define EN_CAST 19      /* a converted to the node's type (the IR's CV or EXT, or nothing) */
#define EN_ARG 20       /* one call argument: a = the value, b = the next EN_ARG */
#define EN_BITF 21       /* a bit-field: a = its bytes' address, val = first bit, c = width, hi8 = signed */

/* binary operators, in the order of bin_ir (emit.c), their IR names;
 * an S or U ending is the signed or unsigned form */
#define B_ADD 0
#define B_SUB 1
#define B_MUL 2
#define B_DIVS 3
#define B_DIVU 4
#define B_REMS 5
#define B_REMU 6
#define B_SHL 7
#define B_SHRS 8
#define B_SHRU 9
#define B_AND 10
#define B_OR 11
#define B_XOR 12
#define B_EQ 13
#define B_NE 14
#define B_LTS 15
#define B_LTU 16
#define B_LES 17
#define B_LEU 18
#define B_GTS 19
#define B_GTU 20
#define B_GES 21
#define B_GEU 22

/* An integer constant is held as val and hi8 so that a 32-bit value fits
 * in ints of 24 bits, the size of the compiler's own int on the Agon; a
 * long long's full 64 bits and a floating constant's bits are in fbits[]
 * (expr.c), at the same index. */
struct enode {
    int type;
    int a;
    int b;
    int c;
    int val;            /* EN_NUM: bits 0..23, as a canonical (sign-extended) int */
    int hi8;            /* EN_NUM: bits 24..31 (for an int-sized constant, its extension); an
                         * int, not a byte: AgDev 3.1.0's LTO build, inlining i32_join, turned
                         * (byte << 8) & 0xFF00 into 0 */
    unsigned char op;           /* EN_* */
};

/* ---- lex.c ------------------------------------------------------------------------ */

/* The current token lives in these globals; next() replaces it. */
extern int tok;             /* current token kind */
extern int tok_val;         /* TK_NUM value: bits 0..23, canonical */
extern int tok_hi8;         /* TK_NUM: bits 24..31 */
extern int tok_type;        /* TK_NUM: T_INT .. T_ULONG, T_LLONG, T_ULLONG, or a floating type */
extern struct sf64 tok_fval;    /* TK_NUM: a floating type's bits (a float's in lo), or an integer's 64 */
extern int tok_name;        /* TK_IDENT: interned name */
extern char tok_str[MAX_STRLIT * 3];     /* TK_STR: a wide string's characters 3 bytes each */
extern int tok_len;         /* TK_STR: character count (no NUL) */
extern int tok_wide;        /* TK_STR: a wide string, L"..." */
extern int tok_line;        /* the current token's line: diagnostics report it */
extern int prev_line;         /* the line of the previous token */
extern char cur_file[128];  /* the source file name from cpp's latest line marker */
extern int errors;          /* errors so far: any makes cc1 fail and remove its output */
extern int warnings_off;    /* -w */
extern int werror;          /* -Werror: warnings are errors */
extern int strict;          /* -ansi: C89's strict mode (c89_spec.md 1) */
extern char names[NAME_TEXT];      /* every interned name, NUL-terminated, end to end */
extern int name_off[MAX_NAMES];     /* name n's text starts at names + name_off[n] */

int lex_open(char *path);
void lex_close(void);
void next(void);
int wide_char_at(char *p);          /* a wide string's character in tok_str */
int peek(void);                     /* the kind of the token after tok */
int peek_name(void);                /* after peek(): its name, if an identifier */
int intern(char *s);                /* the name's number: equal texts, equal numbers */
char *name_str(int n);
/* error and warning go on; fatal reports and stops at once (give_up) */
void error(char *msg);
void error_s(char *msg, char *arg);
void warning_s(char *msg, char *arg);
void warning(char *msg);
void fatal(char *msg);
void give_up(void);                 /* exit 200, removing the partial output */
void expect(int kind, char *what);  /* the current token must be kind: consume it, or stop */
int accept(int kind);               /* consume the current token if it is kind; 1 if it was */

/* ---- type.c ------------------------------------------------------------------------ */

extern struct type types[MAX_TYPES];
extern int plist[MAX_PLIST];
extern struct gsym globals[MAX_GLOBALS];
extern int nglobals;
extern struct lsym locals[MAX_LOCALS];
extern int nlocals;
extern int nparams_cur;     /* the argument slots the parameters so far take */
extern int scope_depth;     /* 0 at file scope, 1 in a function's outermost block, ... */
extern int nblocks;         /* the current function's blocks, numbered from 0 */
extern int block_parent[MAX_BLOCKS];    /* each block's enclosing block (-1: none) */
extern struct tag tags[MAX_TAGS];
extern struct member members[MAX_MEMBERS];

void type_init(void);
int is_struct(int t);
int find_tag(int name);
int add_tag(int name, int is_enum, int local);
int add_member(int tag, int name, int type);
int add_field(int tag, int name, int type, int width);
int find_member(int tag, int name);
void complete_struct(int tag);
int typedef_type(int name);         /* the type a typedef name names, or -1 */
int add_enum_local(int name, int val);
int make_hole(void);
int ptr_to(int t);
int array_of(int t, int count);
int func_type(int ret, int *params, int n, int variadic);
int type_size(int t);
int is_integer(int t);
int is_unsigned(int t);
int is_long(int t);
int is_floating(int t);
int is_double(int t);
int is_llong(int t);
int is_mem8(int t);
int is_arith(int t);
int is_void(int t);
int qualify(int t, int q);
int unqual(int t);
int slots(int t);                   /* argument and local slots: 1, 2 for a long, ceil(size/3) for a struct */
int new_temp(int type);
int add_local_global(int name, int g);
int add_local_typedef(int name, int t);
int local_in_scope(int name);
void scope_function(void);
void scope_enter(void);
void scope_leave(void);
int is_pointer(int t);
int is_scalar(int t);
int decay(int t);
int promote(int t);
int same_type(int a, int b);
int find_global(int name);
int add_global(int name, int kind, int type, int sclass);
int find_local(int name);
int add_local(int name, int type, int param);
void clear_locals(void);
void drop_locals(int mark);
/* -v's table reports, one in each of lex.c, type.c and emit.c */
void lex_report(void);
void type_report(void);
void emit_report(void);

/* ---- expr.c ----------------------------------------------------------------------- */

extern struct enode nodes[MAX_NODES];
extern struct sf64 fbits[MAX_NODES];
extern int nnodes;               /* nodes in use: a statement saves it and resets to it */
extern int peak_nodes;
extern int in_function;          /* parsing a function body (not a file-scope initialiser) */
extern char *cur_func_name;
int compound_literal(int t);      /* that function's name, for __func__ (set by stmt.c) */
extern int fp_used;             /* the function has a float or double value */
extern int ll_used;              /* the function has a long long value */

int node(int op, int type, int a, int b, int val);
int num(int v, int t);
int expression(void);
int assign_expr(void);
int const_expr(void);
void const_expr32(struct i32 *r, int *type);
int case_test64(int tmp, int t, struct i64 *v);   /* a case label: 32 bits and its type */
int convert(int n, int t, char *what);
int truth(int n);
/* these three are defined in stmt.c, with the declaration parser */
int is_type_start(void);
int is_type_start_after_paren(void);
int parse_type_name(void);
int is_lvalue(int n);

/* ---- emit.c -------------------------------------------------------------------------- */

extern FILE *ir_out;
extern char *ir_path;               /* removed on failure (give_up, or errors at the end) */
extern char unit_name[64];          /* the U record's name: the source's base name, or -u */
extern char unit_hex[8];            /* the unit id, 4 hex digits of a hash of the name */
extern int func_strs;               /* S records in the current function */
extern int str_objects;             /* static objects cc1 has named in this unit */
extern int in_static_init;           /* a D is open: string objects become SO items */

void emit_init(void);
void emit_line(char *s);            /* one IR line, written at once (functions are streamed) */
void emit_fmt(char *fmt, int v);
void emit_fmt_s(char *fmt, char *s);
void emit_rvalue(int n);
void emit_cond(int n, int when, int label);
void func_begin(char *sym, int vis_static, int nparams, char *flags);
void func_end(void);
char *ir_sym(int g);
int wide_string(char *bytes, int len);
void wide_flush(void);
void pragma_weak(char *name);
int switch_new(int end_label, int type);
int init_template(int t);
int value_type_of(int n);           /* (defined in expr.c) */
int compatible_funcs(int a, int b); /* (defined in stmt.c) */
int switch_case(int k, struct i32 *v, unsigned long qhi, int label);   /* qhi: a long long's bits 32..63 */
int switch_default(int k, int label);
int switch_has_default(int k);
int switch_type(int k);
int switch_exit(int k);   /* the default's label, or the end's */
int func_string(char *bytes, int len);      /* S record in the function; returns its number */
int file_string(char *bytes, int len);      /* a D __str object; returns its global symbol */
void write_ir(char *s);                     /* straight to the output file */
void write_irstr(char *bytes, int len);     /* the text of an IR string token */
char *ir_string(char *bytes, int len);
extern char *bin_ir[23];

/* ---- stmt.c --------------------------------------------------------------------------- */

extern int verbose;                 /* -v: report each table's peak use at the end */
extern int func_variadic;         /* the function being compiled ends with '...' */
int decl_specs(int *sclass);
int declarator(int base, int *name);

#endif
