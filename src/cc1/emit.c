/* emit.c - IR output (docs/ir_format.md).
 *
 * The back half of cc1: stmt.c parses and expr.c builds each expression
 * as a typed tree; emit_rvalue walks the tree depth first and writes it in
 * postfix order (operands before their operator), one IR record a line,
 * the way a stack machine would evaluate it. cc2 reads the records back,
 * rebuilds the tree and selects eZ80 code for it. The IR has no C types:
 * this file maps each C type to a value kind (I, L, F, D, Q) and, for I,
 * a size and signedness, which is all cc2 needs.
 *
 * Also here: the IR's names for symbols and strings, the static objects
 * cc1 makes for string literals and local aggregates' initial values, the
 * switch tables, and the frame layout at each function's end.
 *
 * A function's IR is written as it is parsed (ir_format.md section 4). The
 * ABI's local layout (abi.md section 5: every scalar in 3-byte slots
 * nearest IX, in declaration order, then arrays and structs, block by
 * block) is only known once all its locals have been seen, so a local is
 * written "LA @k" (k = its number) and func_end writes each one's offset
 * (LOC) and the frame size (FRAME) after the body, where cc2 reads them
 * ahead. A switch's cases are likewise only known after its body: it is
 * written "SW @k" (k = switch number), and its table (SWT and K lines)
 * follows the body too. Streaming means cc1 never holds a whole function,
 * which keeps its memory use small on the Agon.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "io.h"
#include "cc1.h"

FILE *ir_out;
char *ir_path;
char unit_name[64];
char unit_hex[8];
int func_strs;
int str_objects;
int in_static_init;

/* the current function's switches and their cases, kept until func_end
 * writes their tables; a switch is known by its index here */
struct swrec {
    int end_label;
    int def_label;      /* -1: none (default goes to the end) */
    int first;          /* first case, chained by caserec.next */
    int last;           /* the last case, where the next is appended */
    int count;
    int type;           /* the controlling expression's (promoted) type */
};

struct caserec {
    int val;            /* bits 0..23 */
    int hi8;            /* bits 24..31 (a long or long long switch's cases) */
    unsigned long qhi;  /* bits 32..63 (a long long switch's) */
    int label;          /* the IR label of the case's code */
    int next;
};

static struct swrec switches[MAX_SWITCHES];
static int nswitches;
static struct caserec cases[MAX_CASES];
static int ncases;

char *bin_ir[23];       /* each B_* operator's IR name */

void emit_init(void)
{
    bin_ir[B_ADD] = "ADD"; bin_ir[B_SUB] = "SUB"; bin_ir[B_MUL] = "MUL";
    bin_ir[B_DIVS] = "DIVS"; bin_ir[B_DIVU] = "DIVU"; bin_ir[B_REMS] = "REMS"; bin_ir[B_REMU] = "REMU";
    bin_ir[B_SHL] = "SHL"; bin_ir[B_SHRS] = "SHRS"; bin_ir[B_SHRU] = "SHRU";
    bin_ir[B_AND] = "AND"; bin_ir[B_OR] = "OR"; bin_ir[B_XOR] = "XOR";
    bin_ir[B_EQ] = "EQ"; bin_ir[B_NE] = "NE"; bin_ir[B_LTS] = "LTS"; bin_ir[B_LTU] = "LTU";
    bin_ir[B_LES] = "LES"; bin_ir[B_LEU] = "LEU"; bin_ir[B_GTS] = "GTS"; bin_ir[B_GTU] = "GTU";
    bin_ir[B_GES] = "GES"; bin_ir[B_GEU] = "GEU";
}

/* ---- output ------------------------------------------------------------------------ */

/* One IR line. emit_line is the same: functions are streamed, so there is
 * no separate function buffer to write into. */
void write_ir(char *s)
{
    out_str(ir_out, s);
    out_str(ir_out, "\n");
}

void emit_line(char *s)
{
    write_ir(s);
}

/* a line from a printf format with one int, or one string */
void emit_fmt(char *fmt, int v)
{
    char buf[120];

    sprintf(buf, fmt, v);
    emit_line(buf);
}

void emit_fmt_s(char *fmt, char *s)
{
    char buf[120];

    sprintf(buf, fmt, s);
    emit_line(buf);
}

/* A symbol as the IR writes it: a leading '.' for internal linkage
 * (ir_format.md section 3). */
static char symbuf[80];

char *ir_sym(int g)
{
    char *name;

    name = name_str(globals[g].name);
    if (globals[g].sclass != SC_STATIC)
        return name;
    symbuf[0] = '.';
    strcpy(symbuf + 1, name);
    return symbuf;
}

/* #pragma weak name: every reference to it from this unit is weak
 * (ir_format.md 3, WK). */
void pragma_weak(char *name)
{
    out_str(ir_out, "WK ");
    out_str(ir_out, name);
    out_str(ir_out, "\n");
}

/* The text of an IR string token: "..." with \\, \" and \xHH escapes
 * (ir_format.md 4), so any byte, a NUL or a newline included, survives a
 * line-based text file. The worst case, every byte escaped, is 4 bytes
 * each. The result is in a static buffer, valid until the next call. */
static char strbuf[MAX_STRLIT * 4 + 8];

char *ir_string(char *bytes, int len)
{
    int i;
    int n;
    int c;

    n = 0;
    strbuf[n] = '"';
    n++;
    for (i = 0; i < len; i++) {
        c = (unsigned char)bytes[i];
        if (c == '\\' || c == '"') {
            strbuf[n] = '\\';
            strbuf[n + 1] = c;
            n = n + 2;
        } else if (c < 0x20 || c > 0x7E) {
            sprintf(strbuf + n, "\\x%02x", c);
            n = n + 4;
        } else {
            strbuf[n] = c;
            n++;
        }
    }
    strbuf[n] = '"';
    strbuf[n + 1] = 0;
    return strbuf;
}

void write_irstr(char *bytes, int len)
{
    out_str(ir_out, ir_string(bytes, len));
}

/* ---- string literals and static objects --------------------------------------------- */

/* A string literal used in a function: an S record, numbered from 1 in
 * each function; cc2 pools a function's strings after its code. */
int func_string(char *bytes, int len)
{
    char head[16];

    if (func_strs >= MAX_FUNC_STRS)
        fatal("too many string literals in one function (a cc1 table limit)");
    func_strs++;
    sprintf(head, "S %d ", func_strs);
    out_str(ir_out, head);                  /* in pieces: the text can be long */
    out_str(ir_out, ir_string(bytes, len));
    out_str(ir_out, "\n");
    return func_strs;
}

/* A string literal outside a function's code (at file scope, or in a
 * static object's initialiser): a static char array of its own, named
 * __str<unit id>_<n>, with internal linkage and its terminating NUL. Its
 * D record is written at once, unless a D is already open (an
 * initialiser being streamed), when it becomes an SO item inside it.
 * Returns its global symbol. */
int file_string(char *bytes, int len)
{
    char name[40];
    int g;

    str_objects++;
    sprintf(name, "__str%s_%d", unit_hex, str_objects);
    g = add_global(intern(name), SK_VAR, array_of(T_CHAR, len + 1), SC_STATIC);
    globals[g].defined = 1;
    globals[g].used = 1;
    if (in_static_init) {
        /* inside an initialiser being written: an SO item (ir_format.md 3) */
        out_str(ir_out, "SO .");
        out_str(ir_out, name);
        out_str(ir_out, " ");
        write_irstr(bytes, len);
        out_str(ir_out, "\n");
        return g;
    }
    out_str(ir_out, "D .");
    out_str(ir_out, name);
    out_str(ir_out, " s\nS ");
    write_irstr(bytes, len);
    out_str(ir_out, "\nB 0\nE\n");
    return g;
}

/* A wide string literal: a static object of wchar_t, one per byte, and the
 * terminator (c89_spec.md 5). Inside a static initialiser, whose D is open,
 * it is kept and written after it (wide_flush): an SO item holds a byte
 * string, not wchar_t elements. Returns its global. */
#define WIDE_KEEP 2048

/* the kept strings, end to end: each its name, a NUL, its length in
 * decimal, a NUL, then its characters at 3 bytes each */
static char wide_kept[WIDE_KEEP];
static int wide_used;

/* a wide string's D record: one T item (3 bytes) per character */
static void wide_object(char *name, char *bytes, int len)
{
    char buf[48];
    int i;

    sprintf(buf, "D .%s s", name);
    write_ir(buf);
    for (i = 0; i <= len; i++) {
        sprintf(buf, "T %d", i < len ? wide_char_at(bytes + 3 * i) : 0);
        write_ir(buf);
    }
    write_ir("E");
}

int wide_string(char *bytes, int len)
{
    char name[40];
    int g;

    str_objects++;
    sprintf(name, "__wstr%s_%d", unit_hex, str_objects);
    g = add_global(intern(name), SK_VAR, array_of(T_INT, len + 1), SC_STATIC);
    globals[g].defined = 1;
    globals[g].used = 1;
    if (!in_static_init) {
        wide_object(name, bytes, len);
        return g;
    }
    if (wide_used + (int)strlen(name) + 3 * len + 8 > WIDE_KEEP)
        fatal("too many wide strings in one initialiser (a cc1 table limit)");
    strcpy(wide_kept + wide_used, name);        /* name, length, characters */
    wide_used = wide_used + strlen(name) + 1;
    sprintf(wide_kept + wide_used, "%d", len);
    wide_used = wide_used + strlen(wide_kept + wide_used) + 1;
    memcpy(wide_kept + wide_used, bytes, 3 * len);
    wide_used = wide_used + 3 * len;
    return g;
}

/* The wide strings an initialiser used, after its E. */
void wide_flush(void)
{
    char *p;
    char *name;
    int len;

    p = wide_kept;
    while (p < wide_kept + wide_used) {
        name = p;
        p = p + strlen(p) + 1;
        len = atoi(p);
        p = p + strlen(p) + 1;
        wide_object(name, p, len);
        p = p + 3 * len;
    }
    wide_used = 0;
}

/* A static object holding a local aggregate's initial value, which the
 * function copies into the local (C89 makes an aggregate's initialisers
 * constant expressions, so one copy serves every call). Only the symbol
 * is made here; stmt.c writes its D record. In the default mode, elements
 * that are not constants are stored after the copy (stmt.c). */
int init_template(int t)
{
    char name[40];
    int g;

    str_objects++;
    sprintf(name, "__ini%s_%d", unit_hex, str_objects);
    g = add_global(intern(name), SK_VAR, t, SC_STATIC);
    globals[g].defined = 1;
    globals[g].used = 1;
    return g;
}

/* ---- expressions -------------------------------------------------------------------- */

/* An I value's size in memory: 1, 2 or 3 (pointers and enums are 3). */
static int size_of_scalar(int t)
{
    switch (types[t].kind) {
    case TY_CHAR: case TY_SCHAR: case TY_UCHAR: return 1;
    case TY_SHORT: case TY_USHORT: return 2;
    }
    return 3;
}

/* "s" or "u": how a load extends the value, or which form an operation
 * takes; a pointer is unsigned */
static char *sign_of(int t)
{
    switch (types[t].kind) {
    case TY_CHAR: case TY_SCHAR: case TY_SHORT: case TY_INT: case TY_LONG: case TY_LLONG: return "s";
    }
    return "u";
}

/* Load a value of type t from the address on the stack: LD.l, LD.f, or
 * LD with the size and the extension that makes the value canonical. */
static void emit_load(int t)
{
    char buf[20];

    if (is_long(t)) {
        emit_line("LD.l");
        return;
    }
    if (types[t].kind == TY_FLOAT) {
        emit_line("LD.f");
        return;
    }
    sprintf(buf, "LD %d %s", size_of_scalar(t), sign_of(t));
    emit_line(buf);
}

/* The IR's kind of a value of type t (ir_format.md 2): I, L, F, D or Q. */
static int kind_of(int t)
{
    if (is_long(t))
        return 'L';
    if (is_llong(t))
        return 'Q';
    if (types[t].kind == TY_FLOAT)
        return 'F';
    if (is_double(t))
        return 'D';
    return 'I';
}

/* ".l", ".f", ".d" or ".q" by the kind of node n's value. */
static char *suffix(int n)
{
    switch (kind_of(nodes[n].type)) {
    case 'L': return ".l";
    case 'F': return ".f";
    case 'D': return ".d";
    case 'Q': return ".q";
    }
    return "";
}

/* A binary operator's IR name: the floating kinds have no signedness, and
 * divide with DIV. */
static char *op_name(int op, int t)
{
    if (!is_floating(t))
        return bin_ir[op];
    switch (op) {
    case B_DIVS: return "DIV";
    case B_LTS: return "LT";
    case B_LES: return "LE";
    case B_GTS: return "GT";
    case B_GES: return "GE";
    }
    return bin_ir[op];
}

/* A CV operand naming type t's kind, size and sign: i3s, l4u, f4, d8, q8s ... */
static char *cv_name(char *buf, int t)
{
    if (types[t].kind == TY_FLOAT)
        strcpy(buf, "f4");
    else if (is_double(t))
        strcpy(buf, "d8");
    else if (is_llong(t))
        sprintf(buf, "q8%s", sign_of(t));
    else if (is_long(t))
        sprintf(buf, "l4%s", sign_of(t));
    else
        sprintf(buf, "i%d%s", is_pointer(t) ? 3 : size_of_scalar(t), is_pointer(t) ? "u" : sign_of(t));
    return buf;
}

/* Push the address of lvalue n: a global's symbol (A), a parameter's frame
 * offset or a local's number (LA), a string literal's (SA), or for *p the
 * value of p. */
static void emit_addr(int n)
{
    int op;

    op = nodes[n].op;
    if (op == EN_GVAR) {
        emit_fmt_s("A %s", ir_sym(nodes[n].val));
    } else if (op == EN_LVAR) {
        if (locals[nodes[n].val].param)
            emit_fmt("LA %d", locals[nodes[n].val].offset);
        else
            emit_fmt("LA @%d", nodes[n].val);
    } else if (op == EN_DEREF) {
        emit_rvalue(nodes[n].a);
    } else if (op == EN_STR) {
        emit_fmt("SA %d", nodes[n].val);
    } else {
        fatal("internal: address of a non-lvalue");
    }
}

/* A call's result kind (ir_format.md 4): i, l, f, d, q, b (a struct) or v. */
static char *call_kind(int t)
{
    if (is_void(t))
        return "v";
    if (is_struct(t))
        return "b";
    switch (kind_of(t)) {
    case 'L': return "l";
    case 'F': return "f";
    case 'D': return "d";
    case 'Q': return "q";
    }
    return "i";
}

/* Write the IR that pushes node n's value: a post-order walk, each
 * operand's records first and then the operator's, so cc2 can rebuild
 * the tree with a stack. An object of array, struct, function, double or
 * long long type pushes its address, which is how the IR carries those
 * values (ir_format.md 2). */
void emit_rvalue(int n)
{
    int op;
    int t;
    int a;
    int k;
    int s;
    char buf[80];
    char from[8];
    char to[8];
    struct i32 v;

    op = nodes[n].op;
    t = nodes[n].type;
    switch (op) {
    case EN_NUM:
        /* floating and long long constants as their exact bits in hex,
         * integers in decimal */
        if (types[t].kind == TY_FLOAT) {
            sprintf(buf, "C.f %08lx", fbits[n].lo);
            emit_line(buf);
        } else if (is_double(t)) {
            sprintf(buf, "C.d %08lx%08lx", fbits[n].hi, fbits[n].lo);
            emit_line(buf);
        } else if (is_llong(t)) {
            sprintf(buf, "C.q %08lx%08lx", fbits[n].hi, fbits[n].lo);
            emit_line(buf);
        } else if (is_long(t)) {
            i32_join(&v, nodes[n].hi8, nodes[n].val);
            strcpy(buf, "C.l ");
            i32_str(buf + 4, &v, !is_unsigned(t));
            emit_line(buf);
        } else {
            emit_fmt("C %d", nodes[n].val);
        }
        return;
    case EN_STR:
    case EN_GVAR:
    case EN_LVAR:
    case EN_DEREF:
        emit_addr(n);
        if (types[t].kind != TY_ARRAY && types[t].kind != TY_STRUCT && types[t].kind != TY_FUNC && !is_mem8(t))
            emit_load(t);               /* an array, struct, function, double or long long stands for its address */
        return;
    case EN_ADDR:
        emit_addr(nodes[n].a);
        return;
    case EN_BITF:
        /* the unit (one byte, or three), shifted down, masked or sign-extended
         * (shifted up to put the field's top bit at bit 23, then
         * arithmetically back down); no mask is needed for a full byte
         * at bit 0, which LD 1 u already zero-extends */
        emit_rvalue(nodes[n].a);
        emit_line(nodes[n].c <= 8 ? "LD 1 u" : "LD 3 u");
        if (nodes[n].val > 0) {
            emit_fmt("C %d", nodes[n].val);
            emit_line("SHRU");
        }
        if (nodes[n].hi8) {
            if (nodes[n].c < 24) {
                emit_fmt("C %d", 24 - nodes[n].c);
                emit_line("SHL");
                emit_fmt("C %d", 24 - nodes[n].c);
                emit_line("SHRS");
            }
        } else if (nodes[n].c < 24 && !(nodes[n].c == 8 && nodes[n].val == 0)) {
            emit_fmt("C %d", (1 << nodes[n].c) - 1);
            emit_line("AND");
        }
        return;
    case EN_BIN:
        /* the operator is named by its operands' kind, not the result's
         * (a comparison of longs is LTS.l, though its result is an int) */
        emit_rvalue(nodes[n].a);
        emit_rvalue(nodes[n].b);
        sprintf(buf, "%s%s", op_name(nodes[n].val, nodes[nodes[n].a].type), suffix(nodes[n].a));
        emit_line(buf);
        return;
    case EN_NEG:
    case EN_CPL:
    case EN_NOT:
        emit_rvalue(nodes[n].a);
        sprintf(buf, "%s%s", op == EN_NEG ? "NEG" : op == EN_CPL ? "CPL" : "NOT", suffix(nodes[n].a));
        emit_line(buf);
        return;
    case EN_LAND:
    case EN_LOR:
        emit_rvalue(nodes[n].a);
        emit_rvalue(nodes[n].b);
        emit_line(op == EN_LAND ? "LAND" : "LOR");
        return;
    case EN_COND:
        /* SEL, like LAND and LOR, takes whole subtrees: cc2 evaluates
         * only the branch selected */
        emit_rvalue(nodes[n].a);
        emit_rvalue(nodes[n].b);
        emit_rvalue(nodes[n].c);
        emit_line("SEL");
        return;
    case EN_COMMA:
        emit_rvalue(nodes[n].a);
        emit_rvalue(nodes[n].b);
        emit_line("SEQ");
        return;
    case EN_ASSIGN:
        emit_addr(nodes[n].a);
        emit_rvalue(nodes[n].b);
        if (types[t].kind == TY_STRUCT)
            emit_fmt("COPY %d", type_size(t));
        else if (is_long(t))
            emit_line("ST.l");
        else if (types[t].kind == TY_FLOAT)
            emit_line("ST.f");
        else if (is_double(t))
            emit_line("ST.d");
        else if (is_llong(t))
            emit_line("ST.q");
        else
            emit_fmt("ST %d", size_of_scalar(t));
        return;
    case EN_ASGOP:
        emit_addr(nodes[n].a);
        emit_rvalue(nodes[n].b);
        if (is_floating(t))
            sprintf(buf, "ASG%s %s", suffix(n), op_name(nodes[n].val, t));
        else if (is_long(t) || is_llong(t))
            sprintf(buf, "ASG%s %s %s", suffix(n), bin_ir[nodes[n].val], sign_of(t));
        else
            sprintf(buf, "ASG %s %d %s", bin_ir[nodes[n].val], size_of_scalar(t), sign_of(t));
        emit_line(buf);
        return;
    case EN_INC:
        a = nodes[n].a;
        emit_addr(a);
        k = is_pointer(t) ? type_size(types[t].base) : 1;
        sprintf(buf, "%s", nodes[n].val == 0 ? "INCPRE" : nodes[n].val == 1 ? "INCPOST"
                : nodes[n].val == 2 ? "DECPRE" : "DECPOST");
        if (is_long(t) || is_floating(t) || is_llong(t))
            strcat(buf, suffix(n));
        else
            sprintf(buf + strlen(buf), " %d %s %d", size_of_scalar(t), sign_of(t), k);
        emit_line(buf);
        return;
    case EN_CAST:
        emit_rvalue(nodes[n].a);
        s = unqual(decay(nodes[nodes[n].a].type));
        if (is_void(t)) {
            return;
        } else if (kind_of(s) != kind_of(t)) {
            sprintf(buf, "CV %s %s", cv_name(from, s), cv_name(to, t));
            emit_line(buf);
        } else if (kind_of(t) == 'I' && size_of_scalar(t) < 3 && !is_pointer(t)) {
            sprintf(buf, "EXT %d %s", size_of_scalar(t), sign_of(t));
            emit_line(buf);
        }
        return;
    case EN_CALL:
        k = 0;
        for (a = nodes[n].a; a >= 0; a = nodes[a].b) {
            emit_rvalue(nodes[a].a);
            s = nodes[nodes[a].a].type;
            if (is_struct(s) || is_mem8(s))
                emit_fmt("ARGB %d", type_size(s));  /* a struct, double or long long is passed by its bytes */
            else
                emit_line("ARG");
            k++;
        }
        if (is_struct(t) || is_mem8(t)) {
            /* the result object, a temporary, is the hidden first argument */
            emit_fmt("LA @%d", nodes[n].c);
            emit_line("ARG");
            k++;
        }
        if (nodes[n].val < 0) {
            emit_rvalue(nodes[n].b);    /* the function's address, after the arguments */
            sprintf(buf, "CALLI %d %s", k, call_kind(t));
        } else {
            /* kind j marks a call through an implicit declaration, for
             * ld's check of what the function really returns
             * (c89_spec.md 13 item 13) */
            sprintf(buf, "CALL %s %d %s", ir_sym(nodes[n].val), k,
                    globals[nodes[n].val].implicit && !is_void(t) ? "j" : call_kind(t));
        }
        emit_line(buf);
        return;
    }
    fatal("internal: unknown expression node");
}

/* Jump to label when condition n is true (when 1) or false (when 0). */
void emit_cond(int n, int when, int label)
{
    emit_rvalue(truth(n));
    emit_fmt(when ? "JT %d" : "JF %d", label);
}

/* ---- functions ---------------------------------------------------------------------- */

static int peak_strs;
static int peak_switches;
static int peak_cases;

/* the per-function counts are reset by each func_begin, so their peaks
 * are taken before that */
static void note_peaks(void)
{
    if (func_strs > peak_strs)
        peak_strs = func_strs;
    if (nswitches > peak_switches)
        peak_switches = nswitches;
    if (ncases > peak_cases)
        peak_cases = ncases;
}

/* -v: this file's per-function tables, as peak/limit. */
void emit_report(void)
{
    note_peaks();
    printf("cc1: function strings %d/%d, switches %d/%d, cases %d/%d\n",
           peak_strs, MAX_FUNC_STRS, peak_switches, MAX_SWITCHES, peak_cases, MAX_CASES);
}

/* A function's F record (ir_format.md 3), its frame given as "@": the
 * layout comes after the body, from func_end. flags is "-" or the
 * return-kind and variadic letters stmt.c worked out. */
void func_begin(char *sym, int vis_static, int nparams, char *flags)
{
    char buf[120];

    note_peaks();
    sprintf(buf, "F %s %s %d @ %s", sym, vis_static ? "s" : "g", nparams, flags);
    write_ir(buf);
    func_strs = 0;
    nswitches = 0;
    ncases = 0;
}

/* ---- switches -------------------------------------------------------------------- */

/* A new switch of the (promoted) type type, ending at end_label; returns
 * its number, the k of "SW @k". stmt.c records its cases as it parses
 * the body. */
int switch_new(int end_label, int type)
{
    if (nswitches >= MAX_SWITCHES)
        fatal("too many switch statements in one function (a cc1 table limit)");
    switches[nswitches].type = type;
    switches[nswitches].end_label = end_label;
    switches[nswitches].def_label = -1;
    switches[nswitches].first = -1;
    switches[nswitches].last = -1;
    switches[nswitches].count = 0;
    nswitches++;
    return nswitches - 1;
}

/* Add "case v:" to switch k, v already converted to the switch's type
 * (a long long's low 32 bits, qhi its high); returns 0 if the value is
 * already a case. The duplicate search is linear, so a switch with n
 * cases costs n * n / 2 comparisons; n is at most a few hundred. */
int switch_case(int k, struct i32 *v, unsigned long qhi, int label)
{
    int c;
    int val;
    int hi8;

    val = i32_low24(v);
    hi8 = is_long(switches[k].type) || is_llong(switches[k].type) ? i32_high8(v) : 0;
    for (c = switches[k].first; c >= 0; c = cases[c].next)
        if (cases[c].val == val && cases[c].hi8 == hi8 && cases[c].qhi == qhi)
            return 0;
    if (ncases >= MAX_CASES)
        fatal("too many case labels in one function (a cc1 table limit)");
    cases[ncases].val = val;
    cases[ncases].hi8 = hi8;
    cases[ncases].qhi = qhi;
    cases[ncases].label = label;
    cases[ncases].next = -1;
    if (switches[k].last < 0)
        switches[k].first = ncases;
    else
        cases[switches[k].last].next = ncases;
    switches[k].last = ncases;
    switches[k].count++;
    ncases++;
    return 1;
}

/* Set switch k's default; returns 0 if it already has one. */
int switch_default(int k, int label)
{
    if (switches[k].def_label >= 0)
        return 0;
    switches[k].def_label = label;
    return 1;
}

/* (the switch's details, for stmt.c's checks and jumps) */
int switch_has_default(int k)
{
    return switches[k].def_label >= 0;
}

int switch_type(int k)
{
    return switches[k].type;
}

int switch_exit(int k)
{
    return switches[k].def_label >= 0 ? switches[k].def_label : switches[k].end_label;
}

/* Switch k's table: SWT with the default (or the end) and the count, then
 * a K line per case, the value as the switch's type reads it. */
static void write_switch(int k)
{
    char buf[60];
    char num[16];
    int c;
    struct i32 v;

    if (is_llong(switches[k].type))
        return;                         /* its cases are tests in the code (stmt.c) */
    sprintf(buf, "SWT %d %d %d", k, switches[k].def_label >= 0 ? switches[k].def_label : switches[k].end_label,
            switches[k].count);
    write_ir(buf);
    for (c = switches[k].first; c >= 0; c = cases[c].next) {
        if (is_long(switches[k].type)) {
            i32_join(&v, cases[c].hi8, cases[c].val);
            i32_str(num, &v, !is_unsigned(switches[k].type));
        } else {
            sprintf(num, "%d", cases[c].val);
        }
        sprintf(buf, "K %s %d", num, cases[c].label);
        write_ir(buf);
    }
}

/* The end of a function: everything that had to wait for the whole body,
 * then its E. That is the frame layout (FRAME and a LOC per local), the
 * R records, and the switch tables. */
void func_end(void)
{
    int off[MAX_LOCALS];
    int top[MAX_BLOCKS];
    int i;
    int b;
    int frame;
    int deepest;
    int k;
    char buf[160];

    /* ABI section 5: in each block, scalars in 3-byte slots (a long or
     * float in two, a double or long long in three) nearest IX, then
     * arrays and structs (enumerators, typedefs and block-scope externs
     * and statics are locals too, but take no space). The outermost block
     * starts at IX; a nested block starts below everything its parent
     * holds, so sibling blocks share space, as their lifetimes never
     * overlap. The frame is the deepest point any block reaches. Blocks
     * are numbered in the order they open, so a parent's top is known
     * before its children's. cc1's temporaries go below all of it.
     * Offsets are negative: locals lie below IX. */
    deepest = 0;
    for (b = 0; b < nblocks; b++) {
        frame = b == 0 ? 0 : top[block_parent[b]];
        for (i = 0; i < nlocals; i++) {
            k = types[locals[i].type].kind;
            if (locals[i].block == b && !locals[i].param && !locals[i].temp && locals[i].kind == LK_VAR
                && k != TY_ARRAY && k != TY_STRUCT) {
                frame = frame + 3 * slots(locals[i].type);
                off[i] = -frame;
            }
        }
        for (i = 0; i < nlocals; i++) {
            k = types[locals[i].type].kind;
            if (locals[i].block == b && !locals[i].param && !locals[i].temp && locals[i].kind == LK_VAR
                && (k == TY_ARRAY || k == TY_STRUCT)) {
                frame = frame + type_size(locals[i].type);
                off[i] = -frame;
            }
        }
        top[b] = frame;
        if (frame > deepest)
            deepest = frame;
    }
    frame = deepest;
    for (i = 0; i < nlocals; i++) {
        if (locals[i].temp) {
            frame = frame + (is_struct(locals[i].type) ? type_size(locals[i].type) : 3 * slots(locals[i].type));
            off[i] = -frame;
        }
    }
    /* a function with floating values brings in printf's and scanf's
     * floating conversions (ir_format.md 3, R), and one with long long
     * values their ll conversions; a program without either links
     * smaller printf and scanf */
    if (fp_used) {
        write_ir("R __fp_print");
        write_ir("R __fp_scan");
    }
    if (ll_used) {
        write_ir("R __ll_print");
        write_ir("R __ll_scan");
    }
    /* the layout, which cc2 reads ahead (ir_format.md section 4) */
    sprintf(buf, "FRAME %d", frame);
    write_ir(buf);
    for (i = 0; i < nlocals; i++) {
        if (locals[i].temp || (!locals[i].param && locals[i].kind == LK_VAR)) {
            sprintf(buf, "LOC %d %d", i, off[i]);
            write_ir(buf);
        }
    }
    for (k = 0; k < nswitches; k++)
        write_switch(k);
    write_ir("E");
}
