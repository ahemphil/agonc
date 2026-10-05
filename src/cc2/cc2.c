/* cc2.c - the code generator: IR (docs/ir_format.md) to a .s unit
 * (docs/object_format.md), under the ABI in docs/abi.md.
 *
 *     cc2 in.ir out.s [-O] [-fno-offset-branches] [-a] [-v]
 *
 * In the pipeline cc1 writes the .ir file, one per translation unit, and
 * cc2 turns it into a .s file of ;;-marked sections, one per function and
 * per object, which ld joins with the runtime and the libraries into one
 * .asm for ez80asm. The driver passes -O unless it is given -O0.
 *
 * Each statement's postfix IR is rebuilt into a tree and walked with the
 * primary/secondary register model (HL primary, DE secondary, temporaries
 * on the hardware stack). A function's code is kept as instruction records
 * with exact sizes until its E record; then every branch is resolved to a
 * $-relative jr or jp (a jump to the very next instruction disappears), the
 * string pool is placed after the code and addressed as _func+N, and the
 * section is printed. Inline asm has no size cc2 can know, so a branch
 * across it, and the pool of a function containing it, use ez80asm @local
 * labels instead.
 *
 * -fno-offset-branches prints every branch with a real @local label
 * (sizes unchanged), for the T3 differential test; -a annotates each line
 * with cc2's computed offset, for the T2 size test (both in
 * tests/cc2/test_cc2.py). -O runs the peephole pass; -v prints the peak
 * use of the fixed tables.
 *
 * ---- how it works ----
 *
 * Reading. The IR streams, and cc2 holds one function at a time, never
 * the unit. G, D and WK records are printed as they come. For an F record,
 * look_ahead first reads on to its E (a streamed function's frame size,
 * LOC and SWT records, and the number of D temporaries the busiest
 * statement needs); rd_seek then goes back to the body's start and the
 * body is translated.
 *
 * Trees. A statement arrives in postfix (reverse Polish) order: each leaf
 * record pushes a node on tree_stack, and each operator pops its operands
 * and pushes itself. When a record that consumes a value arrives (DROP,
 * RET, JF, JT, SW) the stack holds exactly the root of the statement's
 * tree. Nodes live in nodes[], refer to each other by index, and are all
 * freed at the end of the statement (end_statement).
 *
 * Code generation. gen(n) emits code that leaves node n's value in the
 * primary register: HL for an I value (a canonical 24-bit int or pointer),
 * E:UHL for an L or F value, HL holding the address for a D or Q value or
 * a struct. A binary operator wants its left operand in DE, the secondary,
 * and its right in HL (gen_operands): the left is computed into HL and
 * saved with push hl, the right is computed, and pop de brings the left
 * back. The machine stack holds the pending operands, so no register
 * allocation is needed. Common shapes skip the push and pop (is_simple,
 * de_loadable) or pick a better instruction: a local's load becomes
 * ld hl,(ix+d), a constant operand an immediate, a multiply by a constant
 * shifts and adds. Conditions are compiled for the jump they decide, not
 * for a value (gen_jump), so a comparison feeding JF becomes a flag test
 * and a conditional jump, and && and || become chains of jumps
 * (short-circuit evaluation). What the eZ80 cannot do inline calls a
 * runtime helper (abi.md section 6).
 *
 * Instruction records. Code is not printed as it is made: each
 * instruction is a struct ins in ins[] with its text and its exact size in
 * bytes, and labels and jumps are records of their own. With the whole
 * function and every size known, every branch can be printed as a
 * $-relative offset instead of a label, which keeps ez80asm's label table
 * small (object_format.md section 3), and the peephole pass can edit the
 * code before anything is printed.
 *
 * Branch resolution. resolve gives every jump the short form jr where it
 * is allowed (2 bytes, a signed 8-bit offset, conditions z, nz, c, nc
 * only), computes every address, and lengthens to jp (4 bytes) each jr
 * whose target is out of range. Lengthening one jump can push another out
 * of range, so this repeats until nothing changes; sizes only ever grow,
 * so it ends. A jump with nothing but labels before its target is dropped.
 *
 * String pool. A function's string literals (S records) and its double
 * and long long constants are placed after its code, inside its section,
 * and addressed as _func+offset, so they need no label either. cc1's
 * strings are numbered up from 0, cc2's 8-byte constants down from
 * MAX_STRS - 1 (dconst, next_str).
 *
 * Peephole (-O). Before the branches are resolved, p_rules looks at each
 * instruction and the next few, and rewrites a few known wasteful
 * patterns: a peephole optimiser, so called because it sees the code
 * through a small window. A rule that needs a register to be unused
 * afterwards asks dead_after, a small liveness check over the
 * straight-line code that follows.
 *
 * Sections of this file: limits; node kinds; instruction records; state;
 * errors; text arena; names; emission; trees; code generation (ints,
 * longs, floating point and long long, calls, then gen and gen_jump); IR
 * reading (records to tree nodes); function output (branch resolution);
 * the peephole pass, then print_function; records (switches, data,
 * functions, the unit); main.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "args.h"
#include "io.h"
#include "int24.h"
#include "int32.h"
#include "rd.h"

/* ---- limits (per function unless noted; -v reports the peaks) ---------- */

#define LINE_BUF 2200           /* an S record: 509 chars, each up to 4 as \xHH */
#define MAX_NODES 500           /* per statement (as cc1's) */
#define MAX_STACK 256           /* tree_stack's operands, arg_stack's ARGs */
#define MAX_INS 4000            /* instruction records */
#define TEXT_SIZE 28000         /* copied instruction text (a literal is not copied): about 7 bytes an instruction */
#define MAX_LABELS 3000         /* IR labels 1..MAX_IR_LABELS-1, then cc2's own */
#define MAX_IR_LABELS 1500      /* as cc1's */
#define MAX_STRS 400            /* string literals and 8-byte constants (one pool) */
#define MAX_REFS 200            /* distinct symbols one section references */
#define MAX_LOCALS 300          /* LOC records in one function (as cc1's) */
#define MAX_SWITCHES 100        /* SWT records in one function (as cc1's) */

/* ---- node kinds ---------------------------------------------------------- */

/* A node's op. The leaves are N_C (a constant), N_A (a symbol's address),
 * N_LA (IX + val, a local's or parameter's address) and N_SA (string val's
 * address); the other nodes take their operands in a, b and c, in the
 * order the IR pushed them. */
#define N_C 1
#define N_A 2
#define N_LA 3
#define N_SA 4
#define N_LD 5
#define N_ST 6
#define N_COPY 7
#define N_ASG 8
#define N_INCPRE 9
#define N_INCPOST 10
#define N_DECPRE 11
#define N_DECPOST 12
#define N_NEG 13
#define N_CPL 14
#define N_NOT 15
#define N_EXT 16
#define N_LAND 17
#define N_LOR 18
#define N_SEL 19
#define N_SEQ 20
#define N_CALL 21
#define N_CV 22         /* kind conversion: c = the source's kind, size and sign (cv_code) */
#define N_ARGB 23       /* an argument of val bytes copied from the address a (a struct) */
/* binary operators: N_BIN + index into bin_names */
#define N_BIN 40
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
#define NBIN 23

static char *bin_names[NBIN];
static char *bin_helpers[NBIN];

/* Value kinds (ir_format.md section 2): I is every integer of 1-3 bytes and
 * every pointer, canonical in HL; L is a long, in E:UHL; F a float's bits,
 * in E:UHL like a long; D a double, as the address of its 8 bytes in HL.
 * Floating-point operations call the library's C helpers (lib/libc/fp.c,
 * abi.md 6), which take their parameters in the reverse of the order they
 * are evaluated in, so that each operand is pushed as soon as it is made:
 * __fsub(b, a) is a - b. A D result lives in a temporary cc2 allocates in
 * the frame, below cc1's locals, reused from one statement to the next.
 * Q, a long long, is handled as D is, by address, with the helpers of
 * lib/libc/ll.c. */
#define K_I 0
#define K_L 1
#define K_F 2
#define K_D 3
#define K_Q 4
#define IS_MEM8(k) ((k) == K_D || (k) == K_Q)

/* Kinds, flags and small values are bytes: every table entry costs
 * its size in each of thousands of slots.
 *
 * a, b and c are operand nodes, in the order the IR pushed them: for a
 * memory node a is the address, for SEL they are the condition, then and
 * else, and CALLI's a is the function's address. A CV keeps its source's
 * cv_code in c. val depends on op: a constant (for a D or Q one, its pool
 * entry), an LA offset, a string number, an increment's step, an ASG's
 * operator, a COPY's or ARGB's byte count, a CALL's argument count. */
struct node {
    int size;       /* bytes loaded, stored or made (EXT, ASG, CV ...) */
    int val;
    int sym;        /* text offset of an assembly name, or -1 */
    int a;
    int b;
    int c;
    int args;       /* CALL: first argument node (last C argument), chained by next */
    int next;
    unsigned char op;
    unsigned char kind;     /* K_I, K_L, K_F, K_D or K_Q: the kind of the value the node produces */
    unsigned char okind;    /* an operator's operand kind (a comparison makes K_I) */
    unsigned char sign;     /* 1 signed, 0 unsigned */
    unsigned char hi;       /* a K_L or K_F constant: bits 24..31 (val: 0..23) */
};

/* ---- instruction records -------------------------------------------------- */

/* A function's code as records, printed only once the whole function is
 * known, so that every size, and so every branch offset, is exact. */
#define I_TEXT 1        /* fixed text, known size */
#define I_ASM 2         /* verbatim inline asm, unknown size */
#define I_LABEL 3       /* label definition, size 0 */
#define I_JUMP 4        /* cond + label; size decided at function end */
#define I_POOL 5        /* text + string number; "<text><pool address>" */
#define I_GONE 6        /* removed after the fact (a frameless function's prologue); prints nothing */
#define I_DL 7          /* a jump table entry: dl <the address of label> */
#define I_LADDR 8       /* text + the address of label: "ld de,<address>" */
#define I_STMT 9        /* -O: the end of an expression statement, where no register holds anything; prints nothing */
#define MAX_SW_CASES 300        /* cases in one switch (cc1's per function) */
#define MAX_TABLE 256           /* values a jump table spans */

/* A jump's condition; jr exists only for the first five (jr_ok). */
#define C_ALWAYS 0
#define C_Z 1
#define C_NZ 2
#define C_C 3
#define C_NC 4
#define C_M 5
#define C_P 6
#define C_PE 7
#define C_PO 8

static char *cond_names[9];

struct ins {
    char *text;     /* a string literal, or a copy in text[] */
    int label;      /* I_LABEL, I_JUMP, I_DL, I_LADDR: a label; I_POOL: a pool entry */
    unsigned char kind;
    unsigned char size;     /* bytes; an instruction is at most a few */
    unsigned char cond;
    unsigned char labeled;  /* I_JUMP: print with an @label (spans inline asm) */
};

/* ---- state ---------------------------------------------------------------- */

static struct node nodes[MAX_NODES];
static int nnodes;
static int tree_stack[MAX_STACK];
static int ntree;
static int arg_stack[MAX_STACK];
static int nargstk;

static struct ins ins[MAX_INS];
static int nins;
static char text[TEXT_SIZE];
static int ntext;
static int label_ins[MAX_LABELS];   /* the record that defines each label, or -1 */
static unsigned char label_used[MAX_LABELS];   /* a jump or table refers to it */
static int next_label;              /* cc2's next own label, from MAX_IR_LABELS up */
static int hi_label;                /* the highest IR label the function used */
static int labels_ready;            /* label_ins holds -1 for every unused IR label */
static int str_text[MAX_STRS];      /* IR-escaped text of each string literal */
static int str_len[MAX_STRS];       /* its byte length, NUL excluded */
static unsigned char str_def[MAX_STRS];
static int hi_str;                  /* one past the highest string the function defined */
static int refs[MAX_REFS];          /* the section's ;;ref symbols, as text offsets */
static int nrefs;

/* one switch's cases, read before choosing a compare chain or a table */
static int sw_val[MAX_SW_CASES];
static int sw_lab[MAX_SW_CASES];

/* a streamed function's layout, read ahead (look_ahead) */
static int loc_off[MAX_LOCALS];
static int sw_pos[MAX_SWITCHES];        /* where each SWT record is */
static int sw_line[MAX_SWITCHES];

static char unit_name[64];
static char unit_id[8];
static char func_name[80];
static char func_vis[4];
static int func_has_asm;
static char func_ret;           /* ;;ret's kind: i, l, r or v (F's flags) */
static int implicits[MAX_REFS]; /* symbols called through implicit declarations */
static int nimplicits;
static int epilogue_label;          /* the label every return jumps to */
static int dtemp_base;              /* cc1's frame size: D temporaries go below it */
static int dtemps;                  /* D temporaries the current statement has taken */
static int ndconst;                 /* double constants in the pool, numbered down from MAX_STRS - 1 */

static struct rd in_rd;
static FILE *out_f;
static char *in_path;
static char *out_path;
static int in_line;
static char line[LINE_BUF];
static char *tok[8];                /* split_line's tokens, inside line */
static int ntok;

static int offset_branches;
static int annotate;
static int verbose;
static int peak_ins;
static int peak_text;
static int peak_nodes;

/* ---- errors ---------------------------------------------------------------- */

/* A failed run never leaves a partial output file behind. */
static void give_up(void)
{
    rd_close(&in_rd);                   /* MOS closes nothing at exit */
    if (out_f != NULL) {
        fclose(out_f);
        remove(out_path);
    }
    exit(200);
}

static void internal(char *msg)
{
    fprintf(stderr, "%s:%d: cc2 internal error: %s\n", in_path, in_line, msg);
    give_up();
}

static void limit(char *what)
{
    fprintf(stderr, "%s:%d: cc2: too many %s (raise the limit in cc2.c)\n", in_path, in_line, what);
    give_up();
}

/* ---- text arena ----------------------------------------------------------- */

/* Copies s into text[], returning its offset. The arena holds a
 * function's copied instruction text, names and strings, and is emptied
 * at each F (and for each unit-level D). */
static int save_text(char *s)
{
    int n;
    int at;

    n = strlen(s);
    if (ntext + n + 1 > TEXT_SIZE)
        limit("instruction text bytes");
    at = ntext;
    strcpy(text + ntext, s);
    ntext = ntext + n + 1;
    return at;
}

/* ---- names ---------------------------------------------------------------- */

/* The assembly name of IR symbol `name`, in buf: a leading '.' marks
 * internal linkage (ir_format.md section 3). */
static void asm_name(char *buf, char *name)
{
    if (name[0] == '.')
        sprintf(buf, "__s%s_%s", unit_id, name + 1);
    else
        sprintf(buf, "_%s", name);
}

/* Notes a symbol the current section uses, once, for its ;;ref line, so
 * that ld keeps the section that defines it (object_format.md section 2).
 * A function's reference to itself needs none. */
static void add_ref(char *asmname)
{
    int i;

    if (strcmp(asmname, func_name) == 0)
        return;
    for (i = 0; i < nrefs; i++)
        if (strcmp(text + refs[i], asmname) == 0)
            return;
    if (nrefs >= MAX_REFS)
        limit("referenced symbols in one function");
    refs[nrefs] = save_text(asmname);
    nrefs++;
}

/* A call through an implicit declaration: ;;implicit for ld's check
 * (object_format.md section 8). */
static void add_implicit(char *asmname)
{
    int i;

    for (i = 0; i < nimplicits; i++)
        if (strcmp(text + implicits[i], asmname) == 0)
            return;
    if (nimplicits >= MAX_REFS)
        limit("implicitly declared callees in one function");
    implicits[nimplicits] = save_text(asmname);
    nimplicits++;
}

/* ---- emission ------------------------------------------------------------- */

/* The hot tables are reached through a pointer taken once: every ins[i].x
 * or nodes[n].x would multiply i by the element size. */
static int add_ins(int kind, int size)
{
    struct ins *p;

    if (nins >= MAX_INS)
        limit("instructions in one function");
    p = &ins[nins];
    p->kind = kind;
    p->size = size;
    p->text = NULL;
    p->label = 0;
    p->cond = 0;
    p->labeled = 0;
    nins++;
    return nins - 1;
}

static char fmt[300];           /* an instruction being formatted; emit copies it */
static int optimize;            /* -O: the peephole pass, and I_STMT records for it */

/* s is a string literal, kept as it is, or fmt, which is copied. */
static void emit(char *s, int size)
{
    int i;

    i = add_ins(I_TEXT, size);
    ins[i].text = s == fmt ? text + save_text(s) : s;
}


/* call nn is 4 bytes in ADL mode: the opcode and a 24-bit address. */
static void emit_call(char *helper)
{
    sprintf(fmt, "call %s", helper);
    emit(fmt, 4);
    add_ref(helper);
}

/* A label of cc2's own (for &&, ?:, switches), numbered from
 * MAX_IR_LABELS up so that it never clashes with an IR label. */
static int new_label(void)
{
    if (next_label >= MAX_LABELS)
        limit("labels in one function");
    label_ins[next_label] = -1;
    label_used[next_label] = 0;
    next_label++;
    return next_label - 1;
}

static void emit_label(int l)
{
    int i;

    if (label_ins[l] >= 0)
        internal("label defined twice");
    if (l < MAX_IR_LABELS && l > hi_label)
        hi_label = l;
    i = add_ins(I_LABEL, 0);
    ins[i].label = l;
    label_ins[l] = i;
}

/* A jump record; resolve decides its size (0, 2 for jr, 4 for jp). */
static void emit_jump(int cond, int l)
{
    int i;

    if (l < MAX_IR_LABELS && l > hi_label)
        hi_label = l;
    i = add_ins(I_JUMP, 0);
    ins[i].cond = cond;
    ins[i].label = l;
    label_used[l] = 1;
}

/* "ix+6" / "ix-3" */
static char ixbuf[16];
static char *ixd(int d)
{
    if (d < 0)
        sprintf(ixbuf, "ix-%d", -d);
    else
        sprintf(ixbuf, "ix+%d", d);
    return ixbuf;
}

/* Does d fit the signed 8-bit displacement of (ix+d)? */
static int fits(int d)
{
    return d >= -128 && d <= 127;
}

static void emit_ld_hl_const(int v)
{
    sprintf(fmt, "ld hl,%d", v);
    emit(fmt, 4);
}

/* A = byte; HL = it extended per sign. sbc hl,hl turns the carry into 0
 * or -1 in all 24 bits at once, which matters because HL's upper byte
 * (HLU) cannot be loaded on its own; ld l,a then puts the byte in. */
static void extend_a(int sign)
{
    if (sign) {
        emit("rlca", 1);                /* carry = the sign */
        emit("sbc hl,hl", 2);
        emit("rrca", 1);                /* A as it was */
        emit("ld l,a", 1);
    } else {
        emit("or a", 1);
        emit("sbc hl,hl", 2);
        emit("ld l,a", 1);
    }
}

/* HL = its low `size` bytes, re-extended per sign (the canonical form,
 * abi.md section 3). A 3-byte value is already whole. */
static void extend_hl(int size, int sign)
{
    if (size == 1) {
        emit("ld a,l", 1);
        extend_a(sign);
    } else if (size == 2) {
        if (sign) {
            emit_call("__sext16");
        } else {
            /* HLU cannot be written directly: rebuild HL from zero */
            emit("ld a,h", 1);
            emit("ld c,l", 1);
            emit("ld hl,0", 4);
            emit("ld l,c", 1);
            emit("ld h,a", 1);
        }
    }
}

/* HL = IX + d; beyond the displacement window through DE (abi.md
 * section 5). */
static void emit_lea(int d)
{
    if (fits(d)) {
        sprintf(fmt, "lea hl,%s", ixd(d));
        emit(fmt, 3);
    } else {
        emit("lea hl,ix+0", 3);
        sprintf(fmt, "ld de,%d", d);
        emit(fmt, 4);
        emit("add hl,de", 1);
    }
}

/* HL = the next D temporary of this statement (look_ahead counted them).
 * The temporaries are 8 bytes each, below cc1's locals: the k-th of a
 * statement is at IX - (dtemp_base + 8k), and do_function makes the frame
 * big enough for the most that any statement needs. */
static void dtemp(void)
{
    dtemps++;
    emit_lea(-(dtemp_base + 8 * dtemps));
}

/* A value of kind k onto the stack as a helper's argument: L and F take
 * two slots (the 4-byte object at the lower address), I, D and Q one (a D
 * or Q value is its address). */
static void push_value(int k)
{
    if (k == K_F || k == K_L)
        emit("push de", 1);
    emit("push hl", 1);
}

/* Removes n argument slots after a call, keeping E:UHL if the result is
 * a long or a float (so it pops into BC, not DE). Each pop is 1 byte. */
static void pop_slots(int n, int k)
{
    while (n > 0) {
        emit(k == K_L || k == K_F ? "pop bc" : "pop de", 1);
        n--;
    }
}

/* ---- trees ---------------------------------------------------------------- */

/* A node with the defaults: an int-sized signed I value, no operands. */
static int new_node(int op)
{
    int n;
    struct node *p;

    if (nnodes >= MAX_NODES)
        limit("nodes in one statement");
    n = nnodes;
    nnodes++;
    p = &nodes[n];
    p->op = op;
    p->kind = K_I;
    p->okind = K_I;
    p->hi = 0;
    p->size = 3;
    p->sign = 1;
    p->val = 0;
    p->sym = -1;
    p->a = -1;
    p->b = -1;
    p->c = -1;
    p->args = -1;
    p->next = -1;
    return n;
}

static void push_tree(int n)
{
    if (ntree >= MAX_STACK)
        limit("pending operands");
    tree_stack[ntree] = n;
    ntree++;
}

static int pop_tree(void)
{
    if (ntree == 0)
        internal("operand stack underflow");
    ntree--;
    return tree_stack[ntree];
}

/* ---- code generation ------------------------------------------------------- */

static void gen(int n);
static void gen_jump(int n, int when, int l);
static void gen_operands_l(int n);
static void lbinop(int b);
static void gen_load_l(int a);
static void gen_store_l(int n);
static void gen_asg_l(int n);
static void gen_incdec_l(int n);
static void gen_cv(int n);
static void gen_long_nz(int n);
static void store_lvalue(int a, int size);
static int direct_lvalue2(int a);

/* The root of an expression statement, whose value DROP discards (R2b):
 * a store or an increment there need not produce its value. */
static int void_node = -1;

/* A node whose code leaves DE and the stack untouched. A short's load is
 * left out because a signed one calls __sext16, which clobbers DE, and an
 * LA out of the displacement window goes through DE. */
static int is_simple(int n)
{
    int op;
    int a;

    op = nodes[n].op;
    if (op == N_C || op == N_A || op == N_SA)
        return 1;
    if (op == N_LA)
        return fits(nodes[n].val);
    if (op == N_LD && nodes[n].size != 2) {
        a = nodes[n].a;
        if (nodes[a].op == N_A)
            return 1;
        if (nodes[a].op == N_LA && fits(nodes[a].val))
            return 1;
    }
    return 0;
}

static void emit_symref(char *pattern, int n)
{
    sprintf(fmt, pattern, text + nodes[n].sym);
    add_ref(text + nodes[n].sym);
}

/* LD of an I value: HL = the value at address node a, extended to 24
 * bits. A local or global address goes into the load itself
 * (ld hl,(ix+d), ld hl,(_sym)); any other is computed into HL first. A
 * byte goes through A and extend_a, except an unsigned local, which
 * ld hl,0 / ld l,(ix+d) loads without A. A short is read as three bytes
 * and re-extended: the eZ80's 16-bit (.sis) loads address through MBASE,
 * so they cannot read through a 24-bit pointer. */
static void gen_load(int n)
{
    int a;
    int size;
    int sign;

    a = nodes[n].a;
    size = nodes[n].size;
    sign = nodes[n].sign;
    if (size == 2) {
        /* a short: load three bytes (the third is ignored), re-extend */
        if (nodes[a].op == N_LA && fits(nodes[a].val)) {
            sprintf(fmt, "ld hl,(%s)", ixd(nodes[a].val));
            emit(fmt, 3);
        } else if (nodes[a].op == N_A) {
            emit_symref("ld hl,(%s)", a);
            emit(fmt, 4);
        } else {
            gen(a);
            emit("ld hl,(hl)", 2);
        }
        extend_hl(2, sign);
        return;
    }
    if (nodes[a].op == N_LA && fits(nodes[a].val)) {
        if (size == 3) {
            sprintf(fmt, "ld hl,(%s)", ixd(nodes[a].val));
            emit(fmt, 3);
        } else if (sign) {
            sprintf(fmt, "ld a,(%s)", ixd(nodes[a].val));
            emit(fmt, 3);
            extend_a(1);
        } else {
            emit("ld hl,0", 4);
            sprintf(fmt, "ld l,(%s)", ixd(nodes[a].val));
            emit(fmt, 3);
        }
    } else if (nodes[a].op == N_A) {
        if (size == 3) {
            emit_symref("ld hl,(%s)", a);
            emit(fmt, 4);
        } else {
            emit_symref("ld a,(%s)", a);
            emit(fmt, 4);
            extend_a(sign);
        }
    } else {
        gen(a);
        if (size == 3) {
            emit("ld hl,(hl)", 2);
        } else {
            emit("ld a,(hl)", 1);
            extend_a(sign);
        }
    }
}

/* A = the byte an LD of size 1 (node n) reads, not widened (R2b). */
static void gen_load_a(int n)
{
    int a;

    a = nodes[n].a;
    if (nodes[a].op == N_LA && fits(nodes[a].val)) {
        sprintf(fmt, "ld a,(%s)", ixd(nodes[a].val));
        emit(fmt, 3);
    } else if (nodes[a].op == N_A) {
        emit_symref("ld a,(%s)", a);
        emit(fmt, 4);
    } else {
        gen(a);
        emit("ld a,(hl)", 1);
    }
}

/* Is n a byte load (its value 0..255, or -128..127 if signed)? */
static int is_byte_load(int n)
{
    return nodes[n].op == N_LD && nodes[n].kind == K_I && nodes[n].size == 1;
}

/* Can node n (EQ or NE) compare a byte load with a constant in A? The
 * constant must be one the byte can hold, or the answer is fixed. */
static int byte_compare(int n)
{
    int a;
    int c;
    int v;

    a = nodes[n].a;
    c = nodes[n].b;
    if (!is_byte_load(a) || nodes[c].op != N_C || nodes[c].kind != K_I)
        return 0;
    v = nodes[c].val;
    return nodes[a].sign ? v >= -128 && v <= 127 : v >= 0 && v <= 255;
}

/* Can node n be loaded straight into DE, leaving HL alone? */
static int de_loadable(int n)
{
    int op;
    int a;

    op = nodes[n].op;
    if (op == N_C)
        return nodes[n].kind == K_I;
    if (op == N_A)
        return 1;
    if (op == N_LA)
        return fits(nodes[n].val);
    if (op == N_LD && nodes[n].kind == K_I && nodes[n].size == 3) {
        a = nodes[n].a;
        return nodes[a].op == N_LA && fits(nodes[a].val);
    }
    return 0;
}

/* DE = node n, which de_loadable accepted. */
static void gen_de(int n)
{
    int op;

    op = nodes[n].op;
    if (op == N_C) {
        sprintf(fmt, "ld de,%d", nodes[n].val);
        emit(fmt, 4);
    } else if (op == N_A) {
        emit_symref("ld de,%s", n);
        emit(fmt, 4);
    } else if (op == N_LA) {
        sprintf(fmt, "lea de,%s", ixd(nodes[n].val));
        emit(fmt, 3);
    } else {
        sprintf(fmt, "ld de,(%s)", ixd(nodes[nodes[n].a].val));
        emit(fmt, 3);
    }
}

/* Store HL (value) at the address in DE; HL keeps the value. The eZ80
 * has ld (hl),de but no ld (de),hl, hence the exchanges. */
static void store_hl_at_de(int size)
{
    if (size == 3) {
        emit("ex de,hl", 1);
        emit("ld (hl),de", 2);
        emit("ex de,hl", 1);
    } else if (size == 2) {
        emit("ld a,l", 1);
        emit("ld (de),a", 1);
        emit("inc de", 1);
        emit("ld a,h", 1);
        emit("ld (de),a", 1);
    } else {
        emit("ld a,l", 1);
        emit("ld (de),a", 1);
    }
}

/* ST of an I value: *a = b, leaving the value in HL. cc1 has already put
 * an EXT on a value for a narrow lvalue, so it needs no re-extension here.
 * A local or global destination is written directly; any other address is
 * computed first (the IR's order) and kept on the stack, or in HL when the
 * value loads straight into DE, while the value is made. */
static void gen_store(int n)
{
    int a;
    int size;

    a = nodes[n].a;
    size = nodes[n].size;
    if (size == 2 && direct_lvalue2(a)) {
        gen(nodes[n].b);
        store_lvalue(a, 2);
        return;
    }
    if (size == 2) {
        gen(a);
        emit("push hl", 1);
        gen(nodes[n].b);
        emit("pop de", 1);
        store_hl_at_de(2);
        return;
    }
    if (nodes[a].op == N_LA && fits(nodes[a].val)) {
        gen(nodes[n].b);
        sprintf(fmt, size == 3 ? "ld (%s),hl" : "ld (%s),l", ixd(nodes[a].val));
        emit(fmt, 3);
    } else if (nodes[a].op == N_A) {
        gen(nodes[n].b);
        if (size == 3) {
            emit_symref("ld (%s),hl", a);
            emit(fmt, 4);
        } else {
            emit("ld a,l", 1);
            emit_symref("ld (%s),a", a);
            emit(fmt, 4);
        }
    } else if (size != 2 && de_loadable(nodes[n].b)) {
        /* a value that loads straight into DE: no push and pop (R2b) */
        gen(a);
        gen_de(nodes[n].b);
        emit(size == 3 ? "ld (hl),de" : "ld (hl),e", size == 3 ? 2 : 1);
        if (n != void_node)
            emit("ex de,hl", 1);        /* HL = the value */
    } else {
        gen(a);
        emit("push hl", 1);
        gen(nodes[n].b);
        emit("pop de", 1);
        store_hl_at_de(size);
    }
}

/* DE = left, HL = right  ->  HL = left op right (0/1 for comparisons).
 * The eZ80 has no 24-bit subtract without carry, so or a clears the carry
 * before each sbc hl,de. ld hl,0 leaves the flags alone, so a comparison
 * builds its 0/1 after the test: jr nz,$+3 skips the 1-byte inc hl, and
 * adc hl,hl on a zero HL turns the carry into 0 or 1. Signed order needs
 * the sign-xor-overflow test, so it calls __ilts or __iles; GT and GE are
 * those helpers with the operands exchanged. */
static void binop(int b)
{
    switch (b) {
    case B_ADD:
        emit("add hl,de", 1);
        break;
    case B_SUB:
        emit("ex de,hl", 1);
        emit("or a", 1);
        emit("sbc hl,de", 2);
        break;
    case B_EQ:
    case B_NE:
        emit("or a", 1);
        emit("sbc hl,de", 2);
        emit("ld hl,0", 4);
        emit(b == B_EQ ? "jr nz,$+3" : "jr z,$+3", 2);
        emit("inc hl", 1);
        break;
    case B_LTU:
    case B_GEU:
        emit("ex de,hl", 1);
        emit("or a", 1);
        emit("sbc hl,de", 2);           /* carry iff left < right */
        emit("ld hl,0", 4);
        if (b == B_LTU) {
            emit("adc hl,hl", 2);
        } else {
            emit("jr c,$+3", 2);
            emit("inc hl", 1);
        }
        break;
    case B_GTU:
    case B_LEU:
        emit("or a", 1);
        emit("sbc hl,de", 2);           /* carry iff right < left */
        emit("ld hl,0", 4);
        if (b == B_GTU) {
            emit("adc hl,hl", 2);
        } else {
            emit("jr c,$+3", 2);
            emit("inc hl", 1);
        }
        break;
    case B_GTS:
    case B_GES:
        emit("ex de,hl", 1);
        emit_call(bin_helpers[b]);
        break;
    default:
        emit_call(bin_helpers[b]);
        break;
    }
}

/* Evaluate a binary node's operands: DE = left, HL = right. A left operand
 * that loads straight into DE comes after a complex right one, which saves
 * a push and a pop (R2b). */
static void gen_operands(int n)
{
    if (!is_simple(nodes[n].b) && de_loadable(nodes[n].a)) {
        gen(nodes[n].b);
        gen_de(nodes[n].a);
        return;
    }
    gen(nodes[n].a);
    if (is_simple(nodes[n].b)) {
        emit("ex de,hl", 1);
        gen(nodes[n].b);
    } else {
        emit("push hl", 1);
        gen(nodes[n].b);
        emit("pop de", 1);
    }
}

/* HL *= k inline (R2b), where that is no longer than the 9 bytes of
 * "ex de,hl / ld hl,k / call __imul": a power of two is k's shifts; any
 * other k copies HL to DE, then works down k's bits below the top one,
 * doubling HL for each and adding DE for each 1. mul_cost is the bytes
 * that takes (99: not done inline). This is binary multiplication by
 * shift-and-add, most significant bit first (Horner's rule on k's
 * bits): each add hl,hl is 1 byte and doubles the result so far. */
static int mul_top;             /* k's highest 1 bit and its number of 1s, from mul_cost */
static int mul_ones;

static int mul_cost(int k)
{
    int bit;

    if (k < 1)
        return 99;
    mul_top = 0;
    mul_ones = 0;
    for (bit = 0; bit < 23; bit++) {
        if ((k >> bit) & 1) {
            mul_top = bit;
            mul_ones++;
        }
    }
    if (mul_ones == 1)
        return mul_top;
    return 2 + mul_top + mul_ones - 1;
}

static void mul_const(int k)
{
    int bit;

    mul_cost(k);
    if (mul_ones == 1) {
        for (bit = 0; bit < mul_top; bit++)
            emit("add hl,hl", 1);
        return;
    }
    emit("push hl", 1);
    emit("pop de", 1);
    for (bit = mul_top - 1; bit >= 0; bit--) {
        emit("add hl,hl", 1);
        if ((k >> bit) & 1)
            emit("add hl,de", 1);
    }
}

/* A binary operator on I or L operands (gen_fbinary does the floating and
 * long long ones). A constant multiplier becomes shifts and adds, and a
 * constant addend or subtrahend goes straight into DE. */
static void gen_binary(int n)
{
    int b;
    int r;

    b = nodes[n].op - N_BIN;
    r = nodes[n].b;
    if (nodes[n].okind == K_L) {
        gen_operands_l(n);
        lbinop(b);
        return;
    }
    if (b == B_MUL && nodes[r].op == N_C && mul_cost(nodes[r].val) <= 9) {
        gen(nodes[n].a);
        mul_const(nodes[r].val);
        return;
    }
    if ((b == B_ADD || b == B_SUB) && nodes[r].op == N_C) {
        gen(nodes[n].a);
        sprintf(fmt, "ld de,%d", nodes[r].val);
        emit(fmt, 4);
        if (b == B_ADD) {
            emit("add hl,de", 1);
        } else {
            emit("or a", 1);
            emit("sbc hl,de", 2);
        }
        return;
    }
    gen_operands(n);
    binop(b);
}

/* Load the lvalue at address node `a` (known LA-fitting or A) into HL. */
static void load_lvalue(int a, int size, int sign)
{
    if (size == 2) {
        if (nodes[a].op == N_LA) {
            sprintf(fmt, "ld hl,(%s)", ixd(nodes[a].val));
            emit(fmt, 3);
        } else {
            emit_symref("ld hl,(%s)", a);
            emit(fmt, 4);
        }
        extend_hl(2, sign);
        return;
    }
    if (nodes[a].op == N_LA) {
        if (size == 3) {
            sprintf(fmt, "ld hl,(%s)", ixd(nodes[a].val));
            emit(fmt, 3);
        } else {
            sprintf(fmt, "ld a,(%s)", ixd(nodes[a].val));
            emit(fmt, 3);
            extend_a(sign);
        }
    } else {
        if (size == 3) {
            emit_symref("ld hl,(%s)", a);
            emit(fmt, 4);
        } else {
            emit_symref("ld a,(%s)", a);
            emit(fmt, 4);
            extend_a(sign);
        }
    }
}

/* Store HL (low `size` bytes) to address node `a` (LA-fitting or A). */
static void store_lvalue(int a, int size)
{
    if (size == 2) {
        if (nodes[a].op == N_LA) {
            sprintf(fmt, "ld (%s),l", ixd(nodes[a].val));
            emit(fmt, 3);
            sprintf(fmt, "ld (%s),h", ixd(nodes[a].val + 1));
            emit(fmt, 3);
        } else {
            emit("ld a,l", 1);
            emit_symref("ld (%s),a", a);
            emit(fmt, 4);
            emit("ld a,h", 1);
            emit_symref("ld (%s+1),a", a);
            emit(fmt, 4);
        }
        return;
    }
    if (nodes[a].op == N_LA) {
        sprintf(fmt, size == 3 ? "ld (%s),hl" : "ld (%s),l", ixd(nodes[a].val));
        emit(fmt, 3);
    } else if (size == 3) {
        emit_symref("ld (%s),hl", a);
        emit(fmt, 4);
    } else {
        emit("ld a,l", 1);
        emit_symref("ld (%s),a", a);
        emit(fmt, 4);
    }
}

/* Can a load or store name address node a itself: a local within the
 * displacement window, or a global? */
static int direct_lvalue(int a)
{
    return (nodes[a].op == N_LA && fits(nodes[a].val)) || nodes[a].op == N_A;
}

/* ...and for a short, whose second byte (ix+d+1) must be reachable too. */
static int direct_lvalue2(int a)
{
    return (nodes[a].op == N_LA && fits(nodes[a].val) && fits(nodes[a].val + 1)) || nodes[a].op == N_A;
}

/* ASG of an I lvalue: *a = *a op b, leaving the new value in HL,
 * re-extended to the lvalue's size and sign (ir_format.md section 4). A
 * direct lvalue costs nothing to address, so the right-hand side comes
 * first and *a is loaded after it. Otherwise the address is made first
 * and stays on the stack: [addr] while b is made, then [addr][b] while *a
 * is loaded. */
static void gen_asg(int n)
{
    int a;
    int size;
    int sign;

    a = nodes[n].a;
    size = nodes[n].size;
    sign = nodes[n].sign;
    if (size == 2 ? direct_lvalue2(a) : direct_lvalue(a)) {
        gen(nodes[n].b);                /* HL = rhs */
        emit("push hl", 1);
        load_lvalue(a, size, sign);     /* HL = *addr */
        emit("pop de", 1);
        emit("ex de,hl", 1);            /* DE = *addr, HL = rhs */
        binop(nodes[n].val);
        store_lvalue(a, size);
    } else {
        gen(a);
        emit("push hl", 1);
        gen(nodes[n].b);                /* HL = rhs */
        emit("pop de", 1);              /* DE = addr */
        emit("push de", 1);
        emit("ex de,hl", 1);            /* HL = addr, DE = rhs */
        emit("push de", 1);
        if (size == 3) {
            emit("ld hl,(hl)", 2);
        } else if (size == 2) {
            emit("ld hl,(hl)", 2);
            extend_hl(2, sign);
        } else {
            emit("ld a,(hl)", 1);
            extend_a(sign);
        }
        emit("pop de", 1);
        emit("ex de,hl", 1);            /* DE = *addr, HL = rhs */
        binop(nodes[n].val);
        emit("pop de", 1);              /* DE = addr */
        store_hl_at_de(size);
    }
    extend_hl(size, sign);
}

/* INCPRE ... DECPOST of an I lvalue, by val (1, or a pointer's element
 * size). A post form's value is the old one. Shorts, bytes and 3-byte
 * values each have their own shape; a direct lvalue is loaded and stored
 * in place, any other through its address in HL. */
static void gen_incdec(int n)
{
    int a;
    int size;
    int sign;
    int delta;
    int post;

    a = nodes[n].a;
    size = nodes[n].size;
    sign = nodes[n].sign;
    delta = nodes[n].val;
    if (nodes[n].op == N_DECPRE || nodes[n].op == N_DECPOST)
        delta = -delta;
    post = nodes[n].op == N_INCPOST || nodes[n].op == N_DECPOST;
    if (n == void_node)
        post = 0;                       /* x++; is ++x; (R2b) */
    if (size == 2) {
        /* a short: the new value is stored as two bytes and re-extended */
        if (direct_lvalue2(a)) {
            load_lvalue(a, 2, sign);
            if (post)
                emit("push hl", 1);
            sprintf(fmt, "ld de,%d", delta);
            emit(fmt, 4);
            emit("add hl,de", 1);
            store_lvalue(a, 2);
        } else {
            gen(a);
            emit("push hl", 1);             /* [addr] */
            emit("ld hl,(hl)", 2);
            extend_hl(2, sign);             /* HL = old */
            if (post) {
                emit("pop de", 1);
                emit("push hl", 1);         /* [old] */
                emit("push de", 1);         /* [old][addr] */
            }
            sprintf(fmt, "ld de,%d", delta);
            emit(fmt, 4);
            emit("add hl,de", 1);           /* HL = new */
            emit("pop de", 1);              /* DE = addr */
            store_hl_at_de(2);
        }
        if (post)
            emit("pop hl", 1);
        else if (n != void_node)
            extend_hl(2, sign);
        return;
    }
    if (size == 1) {
        /* byte: A holds the byte throughout; C keeps the old one */
        if (direct_lvalue(a)) {
            load_lvalue(a, 1, sign);
            emit("ld a,l", 1);
        } else {
            gen(a);
            emit("ld a,(hl)", 1);
        }
        emit("ld c,a", 1);
        /* the step modulo 256: the stored byte wraps as the C type does */
        sprintf(fmt, "add a,%d", delta & 255);
        emit(fmt, 2);
        if (direct_lvalue(a)) {
            emit("ld l,a", 1);
            store_lvalue(a, 1);
            emit("ld a,l", 1);
        } else {
            emit("ld (hl),a", 1);
        }
        if (post)
            emit("ld a,c", 1);
        if (n != void_node)
            extend_a(sign);
        return;
    }
    if (direct_lvalue(a)) {
        load_lvalue(a, 3, sign);
        if (post)
            emit("push hl", 1);
        if (delta == 1) {
            emit("inc hl", 1);
        } else if (delta == -1) {
            emit("dec hl", 1);
        } else {
            sprintf(fmt, "ld de,%d", delta);
            emit(fmt, 4);
            emit("add hl,de", 1);
        }
        store_lvalue(a, 3);
        if (post)
            emit("pop hl", 1);
        return;
    }
    gen(a);                             /* HL = addr */
    emit("ld de,(hl)", 2);              /* DE = old */
    emit("push hl", 1);
    if (post)
        emit("push de", 1);
    emit("ex de,hl", 1);                /* HL = old */
    sprintf(fmt, "ld de,%d", delta);
    emit(fmt, 4);
    emit("add hl,de", 1);               /* HL = new */
    if (post) {
        emit("pop bc", 1);              /* BC = old */
        emit("ex de,hl", 1);            /* DE = new */
        emit("pop hl", 1);              /* HL = addr */
        emit("ld (hl),de", 2);
        emit("push bc", 1);
        emit("pop hl", 1);              /* HL = old */
    } else {
        emit("ex de,hl", 1);            /* DE = new */
        emit("pop hl", 1);
        emit("ld (hl),de", 2);
        if (n != void_node)
            emit("ex de,hl", 1);        /* HL = new */
    }
}

/* ---- longs (K_L): E:UHL, a helper's left operand in A:UBC (abi.md 3, 6) ------- */

/* Save a long in E:UHL on the stack; restore_left puts it in A:UBC.
 * push af saves E (copied to A) with the flags; the low 24 bits go last,
 * so they come off first. */
static void save_long(void)
{
    emit("ld a,e", 1);
    emit("push af", 1);
    emit("push hl", 1);
}

static void restore_left(void)
{
    emit("pop bc", 1);
    emit("pop af", 1);
}

/* A:UBC = left, E:UHL = right (or HL = a shift's count). */
static void gen_operands_l(int n)
{
    gen(nodes[n].a);
    save_long();
    gen(nodes[n].b);
    restore_left();
}

/* Exchange A:UBC and E:UHL: ex (sp),hl swaps the low 24 bits through the
 * stack, D carries the high byte across (D is unspecified in a long). */
static void swap_l(void)
{
    emit("push bc", 1);
    emit("ex (sp),hl", 1);
    emit("pop bc", 1);
    emit("ld d,a", 1);
    emit("ld a,e", 1);
    emit("ld e,d", 1);
}

static char *lhelper[NBIN];         /* the 32-bit helper for each binary operator */

/* A:UBC = left, E:UHL = right  ->  E:UHL = left op right, or HL = 0/1
 * with Z set iff 0 for a comparison. */
static void lbinop(int b)
{
    switch (b) {
    case B_ADD:
        /* inline: the low 24 bits, then the high byte with their carry */
        emit("add hl,bc", 1);
        emit("adc a,e", 1);
        emit("ld e,a", 1);
        return;
    case B_NE:
        /* __leq's 0/1 inverted; or a sets Z iff the result is 0, as the
         * compare helpers' contract has it */
        emit_call("__leq");
        emit("ld a,l", 1);
        emit("xor 1", 2);
        emit("ld l,a", 1);
        emit("or a", 1);
        return;
    case B_GTS:
    case B_GTU:
    case B_GES:
    case B_GEU:
        swap_l();
        break;
    default:
        break;
    }
    emit_call(lhelper[b]);
}

/* The long at address node a into E:UHL. (ix+d) works only when both d
 * and d+3 fit the displacement; any other address goes into IY, the
 * scratch index register, for the two indexed loads. */
static void gen_load_l(int a)
{
    if (nodes[a].op == N_LA && fits(nodes[a].val) && fits(nodes[a].val + 3)) {
        sprintf(fmt, "ld hl,(%s)", ixd(nodes[a].val));
        emit(fmt, 3);
        sprintf(fmt, "ld e,(%s)", ixd(nodes[a].val + 3));
        emit(fmt, 3);
    } else if (nodes[a].op == N_A) {
        emit_symref("ld hl,(%s)", a);
        emit(fmt, 4);
        emit_symref("ld a,(%s+3)", a);
        emit(fmt, 4);
        emit("ld e,a", 1);
    } else {
        gen(a);
        emit("push hl", 1);
        emit("pop iy", 2);
        emit("ld hl,(iy+0)", 3);
        emit("ld e,(iy+3)", 3);
    }
}

/* ST.l: the value (b) at the address (a); E:UHL keeps the value. */
static void gen_store_l(int n)
{
    int a;

    a = nodes[n].a;
    if (nodes[a].op == N_LA && fits(nodes[a].val) && fits(nodes[a].val + 3)) {
        gen(nodes[n].b);
        sprintf(fmt, "ld (%s),hl", ixd(nodes[a].val));
        emit(fmt, 3);
        sprintf(fmt, "ld (%s),e", ixd(nodes[a].val + 3));
        emit(fmt, 3);
    } else if (nodes[a].op == N_A) {
        gen(nodes[n].b);
        emit_symref("ld (%s),hl", a);
        emit(fmt, 4);
        emit("ld a,e", 1);
        emit_symref("ld (%s+3),a", a);
        emit(fmt, 4);
    } else {
        gen(a);
        emit("push hl", 1);
        gen(nodes[n].b);
        emit("pop iy", 2);
        emit("ld (iy+0),hl", 3);
        emit("ld (iy+3),e", 3);
    }
}

/* ASG.l: *a = *a op b; E:UHL = the new value. The address stays on the
 * stack across the right-hand side and the operation, which may call
 * helpers that clobber IY. */
static void gen_asg_l(int n)
{
    gen(nodes[n].a);
    emit("push hl", 1);                 /* [addr] */
    gen(nodes[n].b);                    /* E:UHL = rhs (HL: a shift count) */
    emit("pop iy", 2);
    emit("push iy", 2);
    emit("ld bc,(iy+0)", 3);
    emit("ld a,(iy+3)", 3);             /* A:UBC = *addr */
    lbinop(nodes[n].val);
    emit("pop iy", 2);
    emit("ld (iy+0),hl", 3);
    emit("ld (iy+3),e", 3);
}

/* INCPRE.l ... DECPOST.l: +/- 1, added as a 32-bit value in A:UBC (-1 is
 * 255 in A and -1 in BC), the add's carry rippling into the high byte. */
static void gen_incdec_l(int n)
{
    int post;
    int dec;

    post = nodes[n].op == N_INCPOST || nodes[n].op == N_DECPOST;
    dec = nodes[n].op == N_DECPRE || nodes[n].op == N_DECPOST;
    gen(nodes[n].a);
    emit("push hl", 1);
    emit("pop iy", 2);                  /* IY = addr: nothing below calls a helper */
    emit("ld hl,(iy+0)", 3);
    emit("ld e,(iy+3)", 3);             /* E:UHL = old */
    if (post)
        save_long();
    if (dec) {
        emit("ld bc,-1", 4);
        emit("ld a,255", 2);
    } else {
        emit("ld bc,1", 4);
        emit("xor a", 1);
    }
    emit("add hl,bc", 1);
    emit("adc a,e", 1);
    emit("ld e,a", 1);                  /* E:UHL = new */
    emit("ld (iy+0),hl", 3);
    emit("ld (iy+3),e", 3);
    if (post) {
        emit("pop hl", 1);
        emit("pop af", 1);
        emit("ld e,a", 1);              /* E:UHL = old */
    }
}

/* CV: from the operand's kind to the node's. */
static void gen_cv(int n)
{
    int from;

    from = nodes[n].c;
    gen(nodes[n].a);
    if (from / 32 == nodes[n].kind) {
        if (nodes[n].kind == K_I)
            extend_hl(nodes[n].size, nodes[n].sign);
        return;                         /* I to I re-extends; L to L: the same bits */
    }
    if (nodes[n].kind == K_L) {
        /* int-sized to long: HL is canonical per the source's own sign */
        if (from & 1)
            emit_call("__itol");
        else
            emit("ld e,0", 2);
    } else {
        /* long to int-sized: the low bytes, re-extended */
        extend_hl(nodes[n].size, nodes[n].sign);
    }
}

/* A long as a condition, or !: HL = 0/1, Z set iff 0. */
static void gen_long_nz(int n)
{
    gen(n);
    emit_call("__lnz");
}

/* ---- floating point (K_F, K_D): the library's helpers (lib/libc/fp.c) ---- */
/* ---- long long (K_Q): lib/libc/ll.c's, ___q and the operator's name ------ */

/* These helpers are C functions: their arguments are on the stack, and
 * their assembly names follow abi.md section 7's _name rule, so the C name
 * __fadd is the symbol ___fadd. Each operand is pushed as soon as it is
 * made, so the parameters are in the reverse of the operands' order:
 * __fsub(b, a) is a - b. A D or Q result goes to a temporary (dtemp) whose
 * address is pushed last, as the first parameter; the helper returns it in
 * HL. */
static char *fhelper[NBIN];         /* __fadd ... by binary operator, K_F */
static char *dhelper[NBIN];         /* __dadd ... K_D */

/* call ___q<op>, op's letters in lower case: ___qadd, ___qshrs ... */
static void emit_qcall(char *op)
{
    char name[16];
    int i;

    strcpy(name, "___q");
    for (i = 0; op[i] && i < 10; i++)
        name[4 + i] = (char)(op[i] >= 'A' && op[i] <= 'Z' ? op[i] - 'A' + 'a' : op[i]);
    name[4 + i] = 0;
    emit_call(name);
}

/* a op b: E:UHL (F) or HL (the D or Q temporary holding it), or HL = 0/1
 * for a comparison. A Q shift's count is an I, pushed as a Q's address is. */
static void gen_fbinary(int n)
{
    int b;
    int k;

    b = nodes[n].op - N_BIN;
    k = nodes[n].okind;
    gen(nodes[n].a);
    push_value(k);
    gen(nodes[n].b);
    push_value(k);
    if (IS_MEM8(k) && b < B_EQ) {
        dtemp();
        emit("push hl", 1);             /* the result object, the first parameter */
    }
    if (k == K_Q)
        emit_qcall(bin_names[b]);
    else
        emit_call(k == K_F ? fhelper[b] : dhelper[b]);
    /* F: two operands of two slots; D, Q: a slot per address, and one for
     * the result object of an arithmetic operator */
    pop_slots(k == K_F ? 4 : b < B_EQ ? 3 : 2, nodes[n].kind);
}

/* NEG.f flips the sign bit in E; NEG.d, NEG.q and CPL.q make a temporary. */
static void gen_fneg(int n)
{
    gen(nodes[n].a);
    if (nodes[n].kind == K_F) {
        emit("ld a,e", 1);
        emit("xor 128", 2);
        emit("ld e,a", 1);
        return;
    }
    emit("push hl", 1);
    dtemp();
    emit("push hl", 1);
    emit_call(nodes[n].kind == K_D ? "___dneg" : nodes[n].op == N_NEG ? "___qneg" : "___qcpl");
    pop_slots(2, K_D);
}

/* ASG.f, ASG.d, ASG.q: *a = *a op b through __fasg(op, b, p) and the
 * like; a Q shift through __qasgsh(op, count, p). */
static void gen_fasg(int n)
{
    int k;
    int b;

    k = nodes[n].kind;
    b = nodes[n].val;
    gen(nodes[n].a);
    emit("push hl", 1);
    gen(nodes[n].b);
    push_value(k);
    emit_ld_hl_const(b);
    emit("push hl", 1);
    emit_call(k == K_F ? "___fasg" : k == K_D ? "___dasg" : b >= B_SHL && b <= B_SHRU ? "___qasgsh" : "___qasg");
    pop_slots(k == K_F ? 4 : 3, k);
}

/* INCPRE.f ... DECPOST.q: +/- 1 through __fincpre(d, p) and the like; a D
 * or Q post form also passes the object for the old value's copy,
 * __dincpost(r, d, p), and returns its address. */
static void gen_fincdec(int n)
{
    int k;
    int post;
    int dec;

    k = nodes[n].kind;
    post = nodes[n].op == N_INCPOST || nodes[n].op == N_DECPOST;
    dec = nodes[n].op == N_DECPRE || nodes[n].op == N_DECPOST;
    gen(nodes[n].a);
    emit("push hl", 1);
    emit_ld_hl_const(dec ? -1 : 1);
    emit("push hl", 1);
    if (IS_MEM8(k) && post) {
        dtemp();                        /* the old value's copy */
        emit("push hl", 1);
    }
    emit_call(k == K_F ? (post ? "___fincpost" : "___fincpre") : k == K_D ? (post ? "___dincpost" : "___dincpre")
              : (post ? "___qincpost" : "___qincpre"));
    pop_slots(IS_MEM8(k) && post ? 3 : 2, k);
}

/* CV from a Q: to I or L its low bytes, loaded (and re-extended); to F or
 * D through a helper. */
static void gen_qcv_from(int n, int to)
{
    if (to == K_I || to == K_L) {
        emit("push hl", 1);
        emit("pop iy", 2);
        emit("ld hl,(iy+0)", 3);
        emit("ld e,(iy+3)", 3);
        if (to == K_I)
            extend_hl(nodes[n].size, nodes[n].sign);
        return;
    }
    push_value(K_Q);
    if (to == K_D) {
        dtemp();
        emit("push hl", 1);
    }
    if (to == K_F)
        emit_call(nodes[n].c & 1 ? "___qtof" : "___uqtof");
    else
        emit_call(nodes[n].c & 1 ? "___qtod" : "___uqtod");
    pop_slots(to == K_D ? 2 : 1, to);
}

/* CV with a floating or Q side: I is first widened to a long; a result
 * that is I is a long's low bytes, re-extended. */
static void gen_fcv(int n)
{
    int from;
    int fsign;
    int to;

    from = nodes[n].c / 32;
    fsign = nodes[n].c & 1;
    to = nodes[n].kind;
    gen(nodes[n].a);
    if (from == to)
        return;                         /* double and long double, or signed and unsigned Q: the same bits */
    if (from == K_Q) {
        gen_qcv_from(n, to);
        return;
    }
    if (from == K_I) {
        if (fsign)
            emit_call("__itol");
        else
            emit("ld e,0", 2);
        from = K_L;
        fsign = 1;                      /* now a long holding the value: an unsigned int's too */
    }
    if (to == K_Q) {
        push_value(from);
        dtemp();
        emit("push hl", 1);
        if (from == K_L)
            emit_call(fsign ? "___ltoq" : "___ultoq");
        else if (from == K_F)
            emit_call(nodes[n].sign ? "___ftoq" : "___ftouq");
        else
            emit_call(nodes[n].sign ? "___dtoq" : "___dtouq");
        pop_slots(from == K_D ? 2 : 3, to);
        return;
    }
    if (to == K_F || to == K_D) {
        push_value(from);
        if (to == K_D) {
            dtemp();
            emit("push hl", 1);
        }
        if (from == K_L)
            emit_call(to == K_F ? (fsign ? "___ltof" : "___ultof") : (fsign ? "___ltod" : "___ultod"));
        else
            emit_call(to == K_F ? "___dtof" : "___ftod");
        pop_slots((from == K_D ? 1 : 2) + (to == K_D), to);
        return;
    }
    /* to an integer, truncated: E:UHL, then an int-sized result's low bytes */
    push_value(from);
    if (from == K_F)
        emit_call(nodes[n].sign ? "___ftol" : "___ftoul");
    else
        emit_call(nodes[n].sign ? "___dtol" : "___dtoul");
    pop_slots(from == K_D ? 1 : 2, K_L);
    if (to == K_I)
        extend_hl(nodes[n].size, nodes[n].sign);
}

/* CALL and CALLI. call_node linked the arguments in the order to push them
 * (the last C argument first). An I argument is one slot; an L or F two,
 * push de then push hl laying its 4 bytes down little-endian; an ARGB (a
 * struct, or a D or Q value) is copied by __memcpy into ceil(n/3) slots
 * reserved below SP. A call through a pointer goes through __callhl,
 * because the eZ80 has no call (hl). The caller then removes the slots. */
static void gen_call(int n)
{
    int arg;
    int k;
    char name[80];

    k = 0;
    for (arg = nodes[n].args; arg >= 0; arg = nodes[arg].next) {
        if (nodes[arg].op == N_ARGB) {
            /* ceil(n/3) slots below SP, filled from the object's address */
            gen(nodes[arg].a);
            emit("ex de,hl", 1);
            emit_ld_hl_const(-3 * ((nodes[arg].val + 2) / 3));
            emit("add hl,sp", 1);
            emit("ld sp,hl", 1);
            emit("ex de,hl", 1);
            sprintf(fmt, "ld bc,%d", nodes[arg].val);
            emit(fmt, 4);
            emit_call("__memcpy");
            k = k + (nodes[arg].val + 2) / 3;
            continue;
        }
        gen(arg);
        if (nodes[arg].kind == K_L || nodes[arg].kind == K_F) {
            emit("push de", 1);         /* the 4-byte object at the lower address */
            k++;
        }
        emit("push hl", 1);
        k++;
    }
    if (nodes[n].sym < 0) {
        gen(nodes[n].a);                /* HL = the function's address */
        emit_call("__callhl");
    } else {
        strcpy(name, text + nodes[n].sym);
        sprintf(fmt, "call %s", name);
        emit(fmt, 4);
        add_ref(name);
    }
    /* k: argument slots, which the caller removes (a long takes two).
     * A long result is in E:UHL, so it is popped into BC, not DE. */
    if (k <= 4) {
        pop_slots(k, nodes[n].kind);
    } else {
        /* SP += 3k through IY, which no result is in */
        sprintf(fmt, "ld iy,%d", k * 3);
        emit(fmt, 5);
        emit("add iy,sp", 2);
        emit("ld sp,iy", 2);
    }
}

/* Emits the code for node n, leaving its value in the primary register:
 * HL (an I value), E:UHL (an L or F value), or HL holding the address of
 * a D or Q value or a struct. Dispatches on the node's op, then its kind. */
static void gen(int n)
{
    int op;
    int l1;
    int l2;
    int i;

    op = nodes[n].op;
    if (op >= N_BIN) {
        if (nodes[n].okind >= K_F)
            gen_fbinary(n);
        else
            gen_binary(n);
        return;
    }
    switch (op) {
    case N_C:
        if (IS_MEM8(nodes[n].kind)) {
            i = add_ins(I_POOL, 4);     /* a double or long long constant: its bytes in the pool */
            ins[i].text = "ld hl,";
            ins[i].label = nodes[n].val;
            break;
        }
        emit_ld_hl_const(nodes[n].val);
        if (nodes[n].kind == K_L || nodes[n].kind == K_F) {
            sprintf(fmt, "ld e,%d", nodes[n].hi);
            emit(fmt, 2);
        }
        break;
    case N_CV:
        if (nodes[n].kind >= K_F || nodes[n].c / 32 >= K_F)
            gen_fcv(n);
        else
            gen_cv(n);
        break;
    case N_A:
        emit_symref("ld hl,%s", n);
        emit(fmt, 4);
        break;
    case N_LA:
        if (fits(nodes[n].val)) {
            sprintf(fmt, "lea hl,%s", ixd(nodes[n].val));
            emit(fmt, 3);
        } else {
            emit("lea hl,ix+0", 3);
            sprintf(fmt, "ld de,%d", nodes[n].val);
            emit(fmt, 4);
            emit("add hl,de", 1);
        }
        break;
    case N_SA:
        if (nodes[n].val < 0 || nodes[n].val >= MAX_STRS || !str_def[nodes[n].val])
            internal("SA of an undefined string");
        i = add_ins(I_POOL, 4);
        ins[i].text = "ld hl,";
        ins[i].label = nodes[n].val;
        break;
    case N_LD:
        if (nodes[n].kind == K_L || nodes[n].kind == K_F)
            gen_load_l(nodes[n].a);
        else
            gen_load(n);
        break;
    case N_ST:
        if (IS_MEM8(nodes[n].kind)) {
            gen(nodes[n].a);            /* ST.d, ST.q: the 8 bytes copied; HL = the destination */
            emit("push hl", 1);
            gen(nodes[n].b);
            emit("pop de", 1);
            emit("ld bc,8", 4);
            emit_call("__memcpy");
        } else if (nodes[n].kind == K_L || nodes[n].kind == K_F) {
            gen_store_l(n);
        } else {
            gen_store(n);
        }
        break;
    case N_COPY:
        gen(nodes[n].a);
        emit("push hl", 1);
        gen(nodes[n].b);
        emit("pop de", 1);
        sprintf(fmt, "ld bc,%d", nodes[n].val);
        emit(fmt, 4);
        emit_call("__memcpy");
        break;
    case N_ASG:
        if (nodes[n].kind >= K_F)
            gen_fasg(n);
        else if (nodes[n].kind == K_L)
            gen_asg_l(n);
        else
            gen_asg(n);
        break;
    case N_INCPRE:
    case N_INCPOST:
    case N_DECPRE:
    case N_DECPOST:
        if (nodes[n].kind >= K_F)
            gen_fincdec(n);
        else if (nodes[n].kind == K_L)
            gen_incdec_l(n);
        else
            gen_incdec(n);
        break;
    case N_NEG:
        if (nodes[n].kind >= K_F) {
            gen_fneg(n);
            break;
        }
        if (nodes[n].kind == K_L) {
            gen(nodes[n].a);
            emit_call("__lneg");
            break;
        }
        /* 0 - x: with the carry clear, sbc hl,hl is 0 */
        gen(nodes[n].a);
        emit("ex de,hl", 1);
        emit("or a", 1);
        emit("sbc hl,hl", 2);
        emit("sbc hl,de", 2);
        break;
    case N_CPL:
        if (nodes[n].kind == K_Q) {
            gen_fneg(n);
            break;
        }
        if (nodes[n].kind == K_L) {
            gen(nodes[n].a);
            emit_call("__lcpl");
            break;
        }
        /* -1 - x, which is ~x: with the carry set, sbc hl,hl is -1 */
        gen(nodes[n].a);
        emit("ex de,hl", 1);
        emit("scf", 1);
        emit("sbc hl,hl", 2);
        emit("or a", 1);
        emit("sbc hl,de", 2);
        break;
    case N_NOT:
        if (nodes[n].okind == K_L) {
            gen_long_nz(nodes[n].a);
            emit("ld a,l", 1);
            emit("xor 1", 2);
            emit("ld l,a", 1);
            break;
        }
        gen(nodes[n].a);
        emit("ld de,-1", 4);
        emit("add hl,de", 1);           /* carry iff HL was non-zero */
        emit("sbc hl,hl", 2);
        emit("inc hl", 1);
        break;
    case N_EXT:
        gen(nodes[n].a);
        extend_hl(nodes[n].size, nodes[n].sign);
        break;
    case N_LAND:
    case N_LOR:
        /* the value of a condition: its jumps, then 1 or 0 */
        l1 = new_label();
        l2 = new_label();
        gen_jump(n, 0, l1);
        emit("ld hl,1", 4);
        emit_jump(C_ALWAYS, l2);
        emit_label(l1);
        emit("ld hl,0", 4);
        emit_label(l2);
        break;
    case N_SEL:
        l1 = new_label();
        l2 = new_label();
        gen_jump(nodes[n].a, 0, l1);
        gen(nodes[n].b);
        emit_jump(C_ALWAYS, l2);
        emit_label(l1);
        gen(nodes[n].c);
        emit_label(l2);
        break;
    case N_SEQ:
        gen(nodes[n].a);
        gen(nodes[n].b);
        break;
    case N_CALL:
        gen_call(n);
        break;
    default:
        internal("unknown node");
    }
}

/* -O: a signed comparison as a jump, inline. Adding 0x800000 to both sides
 * flips their sign bits, which turns signed order into unsigned order, and
 * the carry of a subtraction gives that. x <= K is x < K + 1 (not for the
 * largest K, which keeps the helper). Returns 0 if it did nothing. */
static int signed_jump(int n, int b, int when, int l)
{
    int k;
    int lt;

    lt = b == B_LTS || b == B_GES;      /* carry means true for LTS, LES; false for GES, GTS */
    if (nodes[nodes[n].b].op == N_C) {
        k = nodes[nodes[n].b].val;
        if (!lt) {
            if (k == 8388607)
                return 0;
            k++;
        }
        gen(nodes[n].a);
        emit("ld de,-8388608", 4);
        emit("add hl,de", 1);           /* the left side, sign flipped */
        sprintf(fmt, "ld de,%d", wrap24(k - 8388608));
        emit(fmt, 4);
        emit("or a", 1);
        emit("sbc hl,de", 2);           /* carry iff left < k, signed */
    } else {
        gen_operands(n);                /* DE = left, HL = right */
        emit("ld bc,-8388608", 4);
        emit("add hl,bc", 1);
        emit("ex de,hl", 1);
        emit("add hl,bc", 1);           /* HL = left, DE = right, signs flipped */
        if (!lt)
            emit("ex de,hl", 1);        /* GTS, LES: right against left */
        emit("or a", 1);
        emit("sbc hl,de", 2);           /* carry iff HL < DE, signed */
        if (!lt) {
            /* carry iff right < left: that is GTS, and LES is its opposite */
            emit_jump(((b == B_GTS) == when) ? C_C : C_NC, l);
            return 1;
        }
    }
    if (b == B_LTS || b == B_LES)
        emit_jump(when ? C_C : C_NC, l);
    else
        emit_jump(when ? C_NC : C_C, l);
    return 1;
}

/* Jump to l if (n != 0) == when; otherwise fall through. A condition is
 * compiled for control flow rather than for its value: a comparison
 * becomes a flag test, ! swaps the sense, && and || become chains of
 * jumps (short-circuit evaluation: b is not evaluated when a decides),
 * and only a general value is tested against zero.
 *
 * The zero test of HL is add hl,de / or a / sbc hl,de: adding DE and
 * subtracting it again gives HL back with Z set iff HL is 0, whatever DE
 * holds. ld a,h / or l would miss HL's upper byte, which cannot be read
 * on its own. */
static void gen_jump(int n, int when, int l)
{
    int op;
    int b;
    int skip;

    op = nodes[n].op;
    if (op == N_NOT) {
        gen_jump(nodes[n].a, !when, l);
        return;
    }
    if (op == N_LAND || op == N_LOR) {
        if ((op == N_LAND) == when) {
            /* a && b true / a || b false: both must agree */
            skip = new_label();
            gen_jump(nodes[n].a, !when, skip);
            gen_jump(nodes[n].b, when, l);
            emit_label(skip);
        } else {
            gen_jump(nodes[n].a, when, l);
            gen_jump(nodes[n].b, when, l);
        }
        return;
    }
    if (op == N_C) {
        if ((nodes[n].val != 0 || nodes[n].hi != 0) == when)
            emit_jump(C_ALWAYS, l);
        return;
    }
    if (op >= N_BIN && nodes[n].okind >= K_F) {
        /* a C helper returns its 0/1 without setting Z to match */
        gen_fbinary(n);                 /* HL = 0/1 */
        emit("add hl,de", 1);
        emit("or a", 1);
        emit("sbc hl,de", 2);           /* Z iff HL = 0 (any DE) */
        emit_jump(when ? C_NZ : C_Z, l);
        return;
    }
    if (op >= N_BIN && nodes[n].okind == K_L && nodes[n].kind == K_I) {
        gen_operands_l(n);
        lbinop(op - N_BIN);             /* HL = 0/1, Z iff false */
        emit_jump(when ? C_NZ : C_Z, l);
        return;
    }
    if (nodes[n].kind == K_L) {
        gen_long_nz(n);
        emit_jump(when ? C_NZ : C_Z, l);
        return;
    }
    if (op >= N_BIN) {
        b = op - N_BIN;
        switch (b) {
        case B_EQ:
        case B_NE:
            if (byte_compare(n)) {
                /* a byte against a byte-sized constant: in A (R2b) */
                gen_load_a(nodes[n].a);
                if (nodes[nodes[n].b].val == 0) {
                    emit("or a", 1);
                } else {
                    sprintf(fmt, "cp %d", nodes[nodes[n].b].val & 255);
                    emit(fmt, 2);
                }
                emit_jump(((b == B_EQ) == when) ? C_Z : C_NZ, l);
                return;
            }
            if (nodes[nodes[n].b].op == N_C) {
                /* against a constant: HL = left, DE = the constant (R2b) */
                gen(nodes[n].a);
                if (nodes[nodes[n].b].val == 0) {
                    emit("add hl,de", 1);
                    emit("or a", 1);
                    emit("sbc hl,de", 2);       /* Z iff HL = 0 (any DE) */
                } else {
                    sprintf(fmt, "ld de,%d", nodes[nodes[n].b].val);
                    emit(fmt, 4);
                    emit("or a", 1);
                    emit("sbc hl,de", 2);
                }
                emit_jump(((b == B_EQ) == when) ? C_Z : C_NZ, l);
                return;
            }
            gen_operands(n);
            emit("or a", 1);
            emit("sbc hl,de", 2);
            emit_jump(((b == B_EQ) == when) ? C_Z : C_NZ, l);
            return;
        case B_LTU:
        case B_GEU:
            if (nodes[nodes[n].b].op == N_C) {
                /* left < constant: carry from left - constant (R2b) */
                gen(nodes[n].a);
                sprintf(fmt, "ld de,%d", nodes[nodes[n].b].val);
                emit(fmt, 4);
                emit("or a", 1);
                emit("sbc hl,de", 2);
                emit_jump(((b == B_LTU) == when) ? C_C : C_NC, l);
                return;
            }
            gen_operands(n);
            emit("ex de,hl", 1);
            emit("or a", 1);
            emit("sbc hl,de", 2);       /* carry iff left < right */
            emit_jump(((b == B_LTU) == when) ? C_C : C_NC, l);
            return;
        case B_GTU:
        case B_LEU:
            gen_operands(n);
            emit("or a", 1);
            emit("sbc hl,de", 2);       /* carry iff right < left */
            emit_jump(((b == B_GTU) == when) ? C_C : C_NC, l);
            return;
        case B_LTS:
        case B_LES:
        case B_GTS:
        case B_GES:
            if (optimize && signed_jump(n, b, when, l))
                return;
            gen_operands(n);
            binop(b);                   /* helper: Z iff false */
            emit_jump(when ? C_NZ : C_Z, l);
            return;
        default:
            break;
        }
    }
    if (is_byte_load(n)) {
        gen_load_a(n);                  /* a byte condition: tested in A (R2b) */
        emit("or a", 1);
        emit_jump(when ? C_NZ : C_Z, l);
        return;
    }
    gen(n);
    emit("add hl,de", 1);
    emit("or a", 1);
    emit("sbc hl,de", 2);               /* Z iff HL = 0 (any DE) */
    emit_jump(when ? C_NZ : C_Z, l);
}

/* ---- IR reading ------------------------------------------------------------ */

/* The next record into line[], its line ending removed; blank lines and
 * whole-line # comments are skipped. 0 at end of file. */
static int read_line(void)
{
    int n;

    for (;;) {
        if (rd_gets(&in_rd, line, LINE_BUF) == 0)
            return 0;
        in_line++;
        n = strlen(line);
        /* a full buffer without a newline: the line was cut short */
        if (n >= LINE_BUF - 1 && line[n - 1] != '\n')
            internal("IR line too long");
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            n--;
            line[n] = 0;
        }
        if (n > 0 && line[0] != '#')
            return 1;
    }
}

/* Splits line into space-separated tokens; a token starting with '"' runs
 * to the matching unescaped '"'. A '#' token starts a comment. */
static void split_line(void)
{
    char *p;

    ntok = 0;
    p = line;
    while (*p && ntok < 8) {
        while (*p == ' ')
            p++;
        if (*p == 0 || *p == '#')
            break;
        tok[ntok] = p;
        ntok++;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1])
                    p++;
                p++;
            }
            if (*p != '"')
                internal("unterminated string");
            p++;
        } else {
            while (*p && *p != ' ')
                p++;
        }
        if (*p) {
            *p = 0;
            p++;
        }
    }
}

/* The first characters are compared before strcmp, which most records
 * would otherwise reach for every name they are tested against. */
static int is(char *s)
{
    return ntok > 0 && tok[0][0] == s[0] && strcmp(tok[0], s) == 0;
}

/* Every IR number is 24-bit two's complement (ir_format.md section 4, for
 * C), read the same whether int is 24 or 32 bits wide (int24.h). */
static int num(int i)
{
    if (i >= ntok)
        internal("missing operand");
    return wrap24(atoi(tok[i]));
}

static int sign_of(int i)
{
    if (i >= ntok || (strcmp(tok[i], "s") != 0 && strcmp(tok[i], "u") != 0))
        internal("expected s or u");
    return tok[i][0] == 's';
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    internal("bad \\x escape");
    return 0;
}

/* Byte length of an IR string token (with its quotes): \xHH is one byte
 * in four characters, any other escape one in two. */
static int string_len(char *s)
{
    int n;

    n = 0;
    s++;
    while (*s && *s != '"') {
        if (*s == '\\')
            s = s + (s[1] == 'x' ? 4 : 2);
        else
            s++;
        n++;
    }
    return n;
}

/* Writes an IR string token as db lines (NUL added if `nul`): runs of
 * printable characters quoted, every other byte (and '"', '\') as a number,
 * so nothing depends on ez80asm's escape rules. */
static unsigned char db_bytes[600];

static void write_db(char *s, int nul)
{
    char buf[256];
    int n;
    int i;
    int len;
    int c;
    int inq;

    n = 0;
    s++;
    while (*s && *s != '"') {
        if (n >= 599)
            internal("string literal too long");
        if (*s == '\\' && s[1] == 'x') {
            db_bytes[n] = hexval(s[2]) * 16 + hexval(s[3]);
            s = s + 4;
        } else if (*s == '\\') {
            db_bytes[n] = s[1];
            s = s + 2;
        } else {
            db_bytes[n] = *s;
            s++;
        }
        n++;
    }
    if (nul) {
        db_bytes[n] = 0;
        n++;
    }
    len = 0;
    inq = 0;
    for (i = 0; i < n; i++) {
        c = db_bytes[i];
        if (len == 0) {
            strcpy(buf, "\tdb ");
            len = strlen(buf);
        } else if (!inq || c < 32 || c > 126 || c == '"' || c == '\\') {
            if (inq) {
                buf[len] = '"';
                len++;
                inq = 0;
            }
            buf[len] = ',';
            len++;
        }
        if (c >= 32 && c <= 126 && c != '"' && c != '\\') {
            if (!inq) {
                buf[len] = '"';
                len++;
                inq = 1;
            }
            buf[len] = c;
            len++;
        } else {
            sprintf(buf + len, "%d", c);
            len = len + strlen(buf + len);
        }
        /* end the line well inside ez80asm's limit (object_format.md
         * section 3: under 256 bytes) */
        if (len > 180 || i == n - 1) {
            if (inq) {
                buf[len] = '"';
                len++;
                inq = 0;
            }
            buf[len] = '\n';
            buf[len + 1] = 0;
            out_str(out_f, buf);
            len = 0;
        }
    }
}
/* An integer binary operator's name as its B_ index; -1 if it is not one. */
static int bin_index(char *s)
{
    int i;

    for (i = 0; i < NBIN; i++)
        if (bin_names[i][0] == s[0] && strcmp(bin_names[i], s) == 0)
            return i;
    return -1;
}

/* A record's name without a kind suffix (".l", ".f", ".d", ".q"), in
 * base_op; returns the suffix's kind. */
static char base_op[16];

static int op_suffix(char *t)
{
    int n;
    int c;

    n = strlen(t);
    if (n >= 16)
        n = 15;
    strncpy(base_op, t, n);
    base_op[n] = 0;
    if (n > 2 && base_op[n - 2] == '.') {
        c = base_op[n - 1];
        if (c != 'l' && c != 'f' && c != 'd' && c != 'q')
            internal("unknown kind suffix");
        base_op[n - 2] = 0;
        return c == 'l' ? K_L : c == 'f' ? K_F : c == 'd' ? K_D : K_Q;
    }
    return K_I;
}

/* Can node n be an operand of kind k? A D or Q value is an address, which
 * an LA or an LD makes as well as the operators of kind D or Q. */
static int of_kind(int n, int k)
{
    return nodes[n].kind == k || (IS_MEM8(k) && nodes[n].kind == K_I);
}

/* A floating operator's name (ADD SUB MUL DIV EQ NE LT LE GT GE) as the
 * index of the integer operator it shares a node with; -1 if it is not
 * one. */
static int fbin_index(char *s)
{
    if (strcmp(s, "DIV") == 0)
        return B_DIVS;
    if (strcmp(s, "LT") == 0)
        return B_LTS;
    if (strcmp(s, "LE") == 0)
        return B_LES;
    if (strcmp(s, "GT") == 0)
        return B_GTS;
    if (strcmp(s, "GE") == 0)
        return B_GES;
    if (strcmp(s, "ADD") == 0 || strcmp(s, "SUB") == 0 || strcmp(s, "MUL") == 0 || strcmp(s, "EQ") == 0
        || strcmp(s, "NE") == 0)
        return bin_index(s);
    return -1;
}

/* Eight hex digits as a 32-bit value (unsigned long is at least 32 bits). */
static unsigned long hex8(char *s)
{
    unsigned long v;
    int i;
    int c;

    v = 0;
    for (i = 0; i < 8; i++) {
        c = s[i];
        if (c >= '0' && c <= '9')
            c = c - '0';
        else if (c >= 'a' && c <= 'f')
            c = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            c = c - 'A' + 10;
        else
            internal("bad hex constant");
        v = v << 4 | (unsigned long)c;
    }
    return v;
}

/* The pool entry after s: cc1's strings, then the double constants. */
static int next_str(int s)
{
    s++;
    if (s >= hi_str && s < MAX_STRS - ndconst)
        s = MAX_STRS - ndconst;
    return s;
}

/* A double or long long constant (16 hex digits, most significant first:
 * a binary64's bits, or the 64-bit integer) as a pool entry: its eight
 * bytes, little-endian, shared by equal constants. Returns its string
 * number (numbered down from the top: cc1's go up from 0). The entry's
 * text is an IR string token, so write_db prints it like any other. */
static int dconst(char *h)
{
    char buf[40];
    int i;
    int s;
    int b;

    if (strlen(h) != 16)
        internal("bad C.d");
    buf[0] = '"';
    for (i = 0; i < 8; i++) {
        b = hexval(h[14 - 2 * i]) * 16 + hexval(h[15 - 2 * i]);
        sprintf(buf + 1 + 4 * i, "\\x%02x", b);
    }
    strcat(buf, "\"");
    for (i = 0; i < ndconst; i++) {
        s = MAX_STRS - 1 - i;
        if (strcmp(text + str_text[s], buf) == 0)
            return s;
    }
    s = MAX_STRS - 1 - ndconst;
    if (s < hi_str || str_def[s])
        limit("string literals and double constants in one function");
    ndconst++;
    str_text[s] = save_text(buf);
    str_len[s] = 8;
    str_def[s] = 1;
    return s;
}

/* Is the record's name, without its kind suffix (base_op), s? */
static int isb(char *s)
{
    return base_op[0] == s[0] && strcmp(base_op, s) == 0;
}

/* A CV operand, "i3s", "l4u", "f4", "d8" or "q8s": kind * 32 + size * 2 +
 * signed. */
static int cv_code(int i)
{
    char *t;

    if (i >= ntok)
        internal("missing CV operand");
    t = tok[i];
    if (strcmp(t, "f4") == 0)
        return K_F * 32 + 8;
    if (strcmp(t, "d8") == 0)
        return K_D * 32 + 16;
    if (t[0] == 'q' && t[1] == '8' && (t[2] == 's' || t[2] == 'u') && t[3] == 0)
        return K_Q * 32 + 16 + (t[2] == 's');
    if ((t[0] != 'i' && t[0] != 'l') || t[1] < '1' || t[1] > '4' || (t[2] != 's' && t[2] != 'u') || t[3] != 0)
        internal("bad CV operand");
    return (t[0] == 'l' ? K_L : K_I) * 32 + (t[1] - '0') * 2 + (t[2] == 's');
}

/* A node naming a symbol (A, CALL): sym is its assembly name in text[]. */
static int sym_node(int op, char *irname)
{
    int n;
    char name[80];

    n = new_node(op);
    asm_name(name, irname);
    nodes[n].sym = save_text(name);
    return n;
}

/* A C record (C, C.l, C.f, C.d, C.q). A long's or a float's 32 bits are
 * split as E:UHL holds them: val the low 24 bits (canonical), hi the top
 * byte. A double's or long long's bytes go to the pool, and val is the
 * entry. */
static int const_node(int lk)
{
    int n;
    struct i32 cval;

    n = new_node(N_C);
    if (lk == K_F) {
        if (ntok < 2 || strlen(tok[1]) != 8)
            internal("bad C.f");
        nodes[n].kind = K_F;
        nodes[n].val = wrap24((int)(hex8(tok[1]) & 0xFFFFFF));
        nodes[n].hi = (int)(hex8(tok[1]) >> 24 & 255);
    } else if (IS_MEM8(lk)) {
        if (ntok < 2)
            internal("bad C.d");
        nodes[n].kind = lk;
        nodes[n].val = dconst(tok[1]);
    } else if (lk == K_L) {
        if (ntok < 2 || !i32_parse(&cval, tok[1]))
            internal("bad C.l");
        nodes[n].kind = K_L;
        nodes[n].val = i32_low24(&cval);
        nodes[n].hi = i32_high8(&cval);
    } else {
        nodes[n].val = num(1);
    }
    return n;
}

/* The records that read or write memory (LD, ST, COPY, ASG, INCPRE ...
 * DECPOST) as a node; -1 if the line is not one. */
static int access_node(int lk)
{
    int n;

    if (isb("LD")) {
        n = new_node(N_LD);
        if (lk == K_L || lk == K_F) {
            nodes[n].kind = lk;
            nodes[n].size = 4;
        } else {
            nodes[n].size = num(1);
            nodes[n].sign = sign_of(2);
        }
        nodes[n].a = pop_tree();
    } else if (isb("ST")) {
        n = new_node(N_ST);
        if (lk != K_I) {
            nodes[n].kind = lk;
            nodes[n].size = IS_MEM8(lk) ? 8 : 4;
        } else {
            nodes[n].size = num(1);
        }
        nodes[n].b = pop_tree();
        nodes[n].a = pop_tree();
    } else if (is("COPY")) {
        n = new_node(N_COPY);
        nodes[n].val = num(1);
        nodes[n].b = pop_tree();
        nodes[n].a = pop_tree();
    } else if (isb("ASG") && (lk == K_F || lk == K_D)) {
        n = new_node(N_ASG);
        if (ntok < 2 || fbin_index(tok[1]) < 0 || fbin_index(tok[1]) > B_DIVS)
            internal("bad ASG.f or ASG.d");
        nodes[n].val = fbin_index(tok[1]);
        nodes[n].kind = lk;
        nodes[n].b = pop_tree();
        nodes[n].a = pop_tree();
    } else if (isb("ASG")) {
        n = new_node(N_ASG);
        if (ntok < 3 || bin_index(tok[1]) < 0)
            internal("bad ASG");
        nodes[n].val = bin_index(tok[1]);
        if (lk == K_L || lk == K_Q) {
            nodes[n].kind = lk;
            nodes[n].size = lk == K_Q ? 8 : 4;
            nodes[n].sign = sign_of(2);
        } else {
            nodes[n].size = num(2);
            nodes[n].sign = sign_of(3);
        }
        nodes[n].b = pop_tree();
        nodes[n].a = pop_tree();
    } else if (isb("INCPRE") || isb("INCPOST") || isb("DECPRE") || isb("DECPOST")) {
        n = new_node(isb("INCPRE") ? N_INCPRE : isb("INCPOST") ? N_INCPOST : isb("DECPRE") ? N_DECPRE : N_DECPOST);
        if (lk >= K_F) {
            nodes[n].kind = lk;
        } else if (lk == K_L) {
            nodes[n].kind = K_L;
            nodes[n].size = 4;
            nodes[n].val = 1;
        } else {
            nodes[n].size = num(1);
            nodes[n].sign = sign_of(2);
            nodes[n].val = num(3);
        }
        nodes[n].a = pop_tree();
    } else {
        return -1;
    }
    return n;
}

/* A CALL or CALLI record, taking its ARGs. */
static int call_node(void)
{
    int n;
    int j;
    int k;
    int arg;

    /* CALL <sym> <nargs> <kind>, or CALLI <nargs> <kind> with the
     * function's address pushed after the arguments */
    if (is("CALL")) {
        n = sym_node(N_CALL, tok[1]);
        j = 2;
    } else {
        n = new_node(N_CALL);
        nodes[n].sym = -1;
        nodes[n].a = pop_tree();
        j = 1;
    }
    k = num(j);
    nodes[n].val = k;
    if (ntok > j + 1 && strcmp(tok[j + 1], "l") == 0)
        nodes[n].kind = K_L;
    else if (ntok > j + 1 && strcmp(tok[j + 1], "f") == 0)
        nodes[n].kind = K_F;
    else if (ntok > j + 1 && strcmp(tok[j + 1], "d") == 0)
        nodes[n].kind = K_D;        /* HL: the result object's address, as for b */
    else if (ntok > j + 1 && strcmp(tok[j + 1], "q") == 0)
        nodes[n].kind = K_Q;
    else if (ntok > j + 1 && strcmp(tok[j + 1], "j") == 0 && j == 2)
        add_implicit(text + nodes[n].sym);
    else if (ntok > j + 1 && strcmp(tok[j + 1], "i") != 0 && strcmp(tok[j + 1], "v") != 0
             && strcmp(tok[j + 1], "b") != 0)
        internal("an unknown CALL kind");
    if (k > nargstk)
        internal("CALL with fewer ARGs than it claims");
    /* The ARGs arrived last C argument first; popping them links each
     * earlier one ahead of the next, leaving the chain in that same
     * order - the order to evaluate and push them (ABI section 4). */
    arg = -1;
    while (k > 0) {
        nargstk--;
        nodes[arg_stack[nargstk]].next = arg;
        arg = arg_stack[nargstk];
        k--;
    }
    nodes[n].args = arg;
    return n;
}

/* One expression-node record; returns 0 if the line is not one. */
static int expr_record(void)
{
    int n;
    int b;
    int k;
    int lk;

    lk = op_suffix(tok[0]);
    n = access_node(lk);
    if (n >= 0) {
        push_tree(n);
        return 1;
    }
    b = lk == K_F || lk == K_D ? fbin_index(base_op) : bin_index(base_op);
    if (b >= 0) {
        /* a binary operator: the right operand was pushed last */
        n = new_node(N_BIN + b);
        nodes[n].b = pop_tree();
        nodes[n].a = pop_tree();
        if (lk == K_F || lk == K_D) {
            nodes[n].okind = lk;
            nodes[n].kind = b >= B_EQ ? K_I : lk;
            if (!of_kind(nodes[n].a, lk) || !of_kind(nodes[n].b, lk))
                internal("a .f or .d operator's operands must be of its kind");
        } else if (lk == K_L || lk == K_Q) {
            nodes[n].okind = lk;
            nodes[n].kind = b >= B_EQ ? K_I : lk;
            if (!of_kind(nodes[n].a, lk) || (!of_kind(nodes[n].b, lk) && b != B_SHL && b != B_SHRS && b != B_SHRU))
                internal("a .l or .q operator's operands must be of its kind");
        }
    } else if (isb("C")) {
        n = const_node(lk);
    } else if (is("CV")) {
        /* the source's code in c; the node's own kind, size and sign are
         * the destination's */
        n = new_node(N_CV);
        nodes[n].c = cv_code(1);
        k = cv_code(2);
        nodes[n].kind = k / 32;
        nodes[n].size = (k / 2) & 15;
        nodes[n].sign = k & 1;
        nodes[n].a = pop_tree();
    } else if (is("A")) {
        n = sym_node(N_A, tok[1]);
    } else if (is("LA")) {
        n = new_node(N_LA);
        if (ntok >= 2 && tok[1][0] == '@') {
            k = atoi(tok[1] + 1);
            if (k < 0 || k >= MAX_LOCALS)
                internal("bad local number");
            nodes[n].val = loc_off[k];
        } else {
            nodes[n].val = num(1);
        }
    } else if (is("SA")) {
        n = new_node(N_SA);
        nodes[n].val = num(1);
    } else if (isb("NEG") || isb("CPL") || isb("NOT")) {
        n = new_node(isb("NEG") ? N_NEG : isb("CPL") ? N_CPL : N_NOT);
        nodes[n].a = pop_tree();
        if (lk >= K_F) {
            /* cc1 writes a floating or long long ! as == 0, and ~ has no floating form */
            if (isb("NOT") || (isb("CPL") && lk != K_Q) || !of_kind(nodes[n].a, lk))
                internal("NOT.f, .d or .q, or CPL.f or .d");
            nodes[n].okind = lk;
            nodes[n].kind = lk;
        } else if (lk == K_L) {
            if (nodes[nodes[n].a].kind != K_L)
                internal("a .l operator's operand must be long");
            nodes[n].okind = K_L;
            nodes[n].kind = isb("NOT") ? K_I : K_L;
        }
    } else if (is("EXT")) {
        n = new_node(N_EXT);
        nodes[n].size = num(1);
        nodes[n].sign = sign_of(2);
        nodes[n].a = pop_tree();
    } else if (is("LAND") || is("LOR")) {
        n = new_node(is("LAND") ? N_LAND : N_LOR);
        nodes[n].b = pop_tree();
        nodes[n].a = pop_tree();
    } else if (is("SEL")) {
        n = new_node(N_SEL);
        nodes[n].c = pop_tree();
        nodes[n].b = pop_tree();
        nodes[n].a = pop_tree();
        nodes[n].kind = nodes[nodes[n].b].kind;
    } else if (is("SEQ")) {
        n = new_node(N_SEQ);
        nodes[n].b = pop_tree();
        nodes[n].a = pop_tree();
        nodes[n].kind = nodes[nodes[n].b].kind;
    } else if (is("ARG") || is("ARGB")) {
        /* an argument waits on arg_stack, not tree_stack, for its CALL */
        if (nargstk >= MAX_STACK)
            limit("pending arguments");
        if (is("ARGB")) {
            n = new_node(N_ARGB);
            nodes[n].val = num(1);
            if (nodes[n].val <= 0)
                internal("bad ARGB size");
            nodes[n].a = pop_tree();
        } else {
            n = pop_tree();
            if (IS_MEM8(nodes[n].kind))
                internal("ARG of a D or Q value (cc1 passes one with ARGB 8)");
        }
        arg_stack[nargstk] = n;
        nargstk++;
        return 1;
    } else if (is("CALL") || is("CALLI")) {
        n = call_node();
    } else {
        return 0;
    }
    push_tree(n);
    return 1;
}

/* After a statement's code: its nodes and D temporaries are free again,
 * and both stacks must be empty (ir_format.md section 4). */
static void end_statement(void)
{
    dtemps = 0;
    if (ntree != 0 || nargstk != 0)
        internal("operand stack not empty at a statement boundary");
    if (nnodes > peak_nodes)
        peak_nodes = nnodes;
    nnodes = 0;
}

/* ---- function output ------------------------------------------------------- */

static int ins_addr[MAX_INS + 1];   /* each record's offset; [nins]: the code size */
static int str_off[MAX_STRS];       /* each pool entry's offset in the pool */

/* jr takes only no condition, z, nz, c or nc. */
static int jr_ok(int cond)
{
    return cond <= C_NC;
}

/* Is the jump at i elided: only labels between it and its target (and,
 * under -O, removed records and statement ends)? */
static int elided(int i)
{
    int t;
    int k;

    t = label_ins[ins[i].label];
    if (t <= i)
        return 0;
    for (k = i + 1; k < t; k++)
        if (ins[k].kind != I_LABEL && !(optimize && (ins[k].kind == I_GONE || ins[k].kind == I_STMT)))
            return 0;
    return 1;
}

/* Is there inline asm between the jump at i and its target? Its distance
 * is then unknown, so the jump must be a jp to an @label. */
static int spans_asm(int i)
{
    int t;
    int lo;
    int hi;
    int k;

    t = label_ins[ins[i].label];
    lo = i < t ? i : t;
    hi = i < t ? t : i;
    for (k = lo; k <= hi; k++)
        if (ins[k].kind == I_ASM)
            return 1;
    return 0;
}

/* Gives every jump its size: 0 if elided, else jr (2) where the condition
 * allows and the target is within -128..127 bytes, else jp (4). Every
 * candidate starts as a jr; each round recomputes the addresses and
 * lengthens the jr's now out of range, until a round changes nothing.
 * Because a jump only ever grows, the rounds end. Also fills ins_addr. */
static void resolve(void)
{
    int i;
    int changed;
    int addr;
    int d;
    struct ins *p;

    for (i = 0, p = ins; i < nins; i++, p++) {
        if (p->kind == I_JUMP) {
            if (label_ins[p->label] < 0)
                internal("jump to an undefined label");
            if (elided(i)) {
                p->size = 0;
            } else {
                p->labeled = func_has_asm && spans_asm(i);
                p->size = (jr_ok(p->cond) && !p->labeled) ? 2 : 4;
            }
        }
    }
    do {
        changed = 0;
        addr = 0;
        for (i = 0, p = ins; i < nins; i++, p++) {
            ins_addr[i] = addr;
            addr = addr + p->size;
        }
        ins_addr[nins] = addr;
        for (i = 0, p = ins; i < nins; i++, p++) {
            if (p->kind == I_JUMP && p->size == 2) {
                /* jr's offset counts from the end of its 2 bytes */
                d = ins_addr[label_ins[p->label]] - (ins_addr[i] + 2);
                if (d < -128 || d > 127) {
                    p->size = 4;
                    changed = 1;
                }
            }
        }
    } while (changed);
}

/* One instruction line; with -a, its offset as a comment (not in a
 * function with inline asm, whose offsets cc2 does not know). */
static void print_line(char *s, int i)
{
    char buf[400];

    if (annotate && i >= 0 && !func_has_asm) {
        sprintf(buf, "\t%s\t; @%d\n", s, ins_addr[i]);
        out_str(out_f, buf);
        return;
    }
    fwrite("\t", 1, 1, out_f);          /* one tab, not eight spaces (R2b) */
    out_str(out_f, s);
    fwrite("\n", 1, 1, out_f);
}

/* ---- the peephole pass (-O) ----------------------------------------------
 * After a function's records are made and before its jumps are sized, a
 * few rules rewrite short runs of instructions. cc2 runs it only when
 * given -O (the driver passes -O unless it is given -O0), and it is never
 * needed for correctness: every rule keeps what the code computes, and
 * every rewritten record keeps an exact size. A rule that needs a register
 * to be dead asks dead_after, which follows the straight-line code after
 * it and answers "live" at anything it does not understand (a label, a
 * jump, an instruction effects does not decode).
 *
 * The rules read the instruction text itself, so effects decodes the few
 * forms cc2 emits into the registers each one reads and writes. Registers
 * are bit sets: HL and DE are split so that writing L or E alone does not
 * count as writing the pair (its other bytes survive); BC is tracked
 * whole. */

#define R_A 1
#define R_F 2
#define R_BC 4
#define R_DH 8          /* DE but E: DEU and D */
#define R_DL 16         /* E */
#define R_DE (R_DH | R_DL)
#define R_HH 32         /* HL but L */
#define R_HL8 64        /* L */
#define R_HL (R_HH | R_HL8)
#define R_IY 128
#define R_SP 256

static char p_mn[8];
static char p_o1[48];
static char p_o2[48];

/* An instruction's mnemonic and up to two operands, into p_mn, p_o1, p_o2. */
static void p_split(char *t)
{
    int i;
    int k;

    for (i = 0; t[i] && t[i] != ' ' && i < 7; i++)
        p_mn[i] = t[i];
    p_mn[i] = 0;
    while (t[i] == ' ')
        i++;
    for (k = 0; t[i] && t[i] != ',' && k < 47; i++, k++)
        p_o1[k] = t[i];
    p_o1[k] = 0;
    if (t[i] == ',')
        i++;
    for (k = 0; t[i] && k < 47; i++, k++)
        p_o2[k] = t[i];
    p_o2[k] = 0;
}

/* The register an operand names (not an address in brackets); DE and HL
 * by parts, so that writing E or L leaves the rest as it was. */
static int p_reg(char *o)
{
    if (strcmp(o, "a") == 0)
        return R_A;
    if (strcmp(o, "hl") == 0)
        return R_HL;
    if (strcmp(o, "h") == 0)
        return R_HH;
    if (strcmp(o, "l") == 0)
        return R_HL8;
    if (strcmp(o, "de") == 0)
        return R_DE;
    if (strcmp(o, "d") == 0)
        return R_DH;
    if (strcmp(o, "e") == 0)
        return R_DL;
    if (strcmp(o, "bc") == 0 || strcmp(o, "b") == 0 || strcmp(o, "c") == 0)
        return R_BC;
    if (strcmp(o, "iy") == 0)
        return R_IY;
    if (strcmp(o, "sp") == 0)
        return R_SP;
    if (strcmp(o, "af") == 0)
        return R_A | R_F;
    return 0;
}

/* One byte of a register pair: writing it keeps the rest. */
static int p_part(char *o)
{
    return o[0] && o[1] == 0 && o[0] != 'a';
}

/* B or C, whose pair is tracked whole: writing one reads the other. */
static int p_bc_part(char *o)
{
    return (o[0] == 'b' || o[0] == 'c') && o[1] == 0;
}

/* The registers an operand reads: itself, or the ones in its address. */
static int p_uses(char *o)
{
    if (o[0] != '(')
        return p_reg(o);
    if (strncmp(o, "(hl", 3) == 0)
        return R_HL;
    if (strncmp(o, "(de", 3) == 0)
        return R_DE;
    if (strncmp(o, "(bc", 3) == 0)
        return R_BC;
    if (strncmp(o, "(iy", 3) == 0)
        return R_IY;
    if (strncmp(o, "(sp", 3) == 0)
        return R_SP;
    return 0;                           /* (ix+d), (_sym): no register but ix */
}

static int hex_digit(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

/* Is a call's target a C function (its arguments on the stack)? */
static int c_function(char *n)
{
    if (n[0] == '_' && n[1] != '_')
        return 1;                       /* _name */
    if (strncmp(n, "___", 3) == 0)
        return 1;                       /* ___name: the library's C helpers */
    return strncmp(n, "__s", 3) == 0 && hex_digit(n[3]) && hex_digit(n[4]) && hex_digit(n[5])
           && hex_digit(n[6]) && n[7] == '_';       /* __s<id>_name */
}

/* What instruction t reads and writes, into *rd and *wr; 0 if unknown
 * (a jump, a rotate, ex (sp),hl ...), which callers treat as the end of
 * what they can follow. */
static int effects(char *t, int *rd, int *wr)
{
    int r;

    p_split(t);
    *rd = 0;
    *wr = 0;
    if (strcmp(p_mn, "ld") == 0) {
        *rd = p_uses(p_o2);
        if (p_o1[0] == '(') {
            *rd = *rd | p_uses(p_o1);
        } else if (p_bc_part(p_o1)) {
            *rd = *rd | R_BC;           /* B or C: the rest of BC is kept */
        } else {
            *wr = p_reg(p_o1);
        }
        return *wr != 0 || p_o1[0] == '(' || p_bc_part(p_o1);
    }
    if (strcmp(p_mn, "lea") == 0) {
        *wr = p_reg(p_o1);
        return 1;
    }
    if (strcmp(p_mn, "push") == 0) {
        *rd = p_reg(p_o1) | R_SP;
        *wr = R_SP;
        return 1;
    }
    if (strcmp(p_mn, "pop") == 0) {
        *rd = R_SP;
        *wr = p_reg(p_o1) | R_SP;
        return 1;
    }
    if (strcmp(p_mn, "ex") == 0 && strcmp(p_o1, "de") == 0 && strcmp(p_o2, "hl") == 0) {
        *rd = R_DE | R_HL;
        *wr = R_DE | R_HL;
        return 1;
    }
    if (strcmp(p_mn, "add") == 0 || strcmp(p_mn, "adc") == 0 || strcmp(p_mn, "sbc") == 0
        || strcmp(p_mn, "sub") == 0) {
        r = p_o2[0] ? p_reg(p_o1) : R_A;        /* sub n: a is implied */
        *rd = r | p_uses(p_o2[0] ? p_o2 : p_o1);
        if (strcmp(p_mn, "adc") == 0 || strcmp(p_mn, "sbc") == 0)
            *rd = *rd | R_F;
        *wr = r | R_F;
        return r != 0;
    }
    if (strcmp(p_mn, "and") == 0 || strcmp(p_mn, "or") == 0 || strcmp(p_mn, "xor") == 0
        || strcmp(p_mn, "cp") == 0) {
        *rd = R_A | p_uses(p_o1);
        *wr = p_mn[0] == 'c' ? R_F : R_A | R_F;
        return 1;
    }
    if (strcmp(p_mn, "inc") == 0 || strcmp(p_mn, "dec") == 0) {
        r = p_uses(p_o1);
        *rd = r;
        if (p_o1[0] == '(')
            *wr = R_F;                  /* (hl): memory and the flags */
        else if (p_part(p_o1) || strcmp(p_o1, "a") == 0)
            *wr = R_F | (p_bc_part(p_o1) ? 0 : r);      /* 8 bits: and the flags */
        else
            *wr = r;                    /* a 16- or 24-bit inc/dec keeps the flags */
        return 1;
    }
    if (strcmp(p_mn, "scf") == 0) {
        *wr = R_F;
        return 1;
    }
    if (strcmp(p_mn, "ret") == 0 && p_o1[0] == 0) {
        /* the function's value: HL (none for void), and E too for a long
         * or a float */
        *rd = R_SP | (func_ret != 'v' ? R_HL : 0) | (func_ret == 'l' ? R_DE : 0);
        return 1;
    }
    if (strcmp(p_mn, "call") == 0) {
        /* a C function (_name, a static's __s<id>_name, the library's
         * ___name) takes its arguments on the stack; a runtime helper
         * (__name) in registers. Neither reads the flags, and both may
         * change every register. */
        *rd = c_function(p_o1) ? R_SP : R_A | R_BC | R_DE | R_HL | R_IY | R_SP;
        *wr = R_A | R_F | R_BC | R_DE | R_HL | R_IY;
        return 1;
    }
    return 0;
}

/* The next record after i that is not gone, if it is an ordinary
 * instruction; -1 at the end or at a label, jump or anything else. */
static int p_next(int i)
{
    for (i++; i < nins; i++) {
        if (ins[i].kind == I_GONE || ins[i].kind == I_STMT)
            continue;
        return ins[i].kind == I_TEXT && ins[i].text != NULL ? i : -1;
    }
    return -1;
}

/* Is the end of a statement next after record i (removed records aside)? */
static int p_stmt_next(int i)
{
    for (i++; i < nins && ins[i].kind == I_GONE; i++)
        ;
    return i < nins && ins[i].kind == I_STMT;
}

/* Is register reg dead after record i: written before it is read, in the
 * straight-line code that follows? This is a liveness check limited to 24
 * records. The end of an expression statement (I_STMT) answers dead: no
 * value is kept in a register from one statement to the next. */
static int dead_after(int i, int reg)
{
    int k;
    int rd;
    int wr;

    for (k = 0; k < 24; k++) {
        if (p_stmt_next(i))
            return 1;
        i = p_next(i);
        if (i < 0 || !effects(ins[i].text, &rd, &wr))
            return 0;
        if (rd & reg)
            return 0;
        reg = reg & ~wr;                /* a part written first is dead */
        if (reg == 0)
            return 1;
    }
    return 0;
}

static void p_gone(int i)
{
    ins[i].kind = I_GONE;
    ins[i].size = 0;
}

/* Replaces record i's instruction: t is a literal, or fmt, copied. */
static void p_set(int i, char *t, int size)
{
    ins[i].text = t == fmt ? text + save_text(t) : t;
    ins[i].size = size;
}

static int p_is(char *t, char *s)
{
    return strcmp(t, s) == 0;
}

/* t is "<prefix><rest>": then rest, else NULL. */
static char *after(char *t, char *prefix)
{
    int n;

    n = strlen(prefix);
    return strncmp(t, prefix, n) == 0 ? t + n : NULL;
}

/* An ix-relative operand "(ix+d)" or "(ix-d)": its offset in *d. */
static int ix_off(char *o, int *d)
{
    if (strncmp(o, "(ix", 3) != 0 || (o[3] != '+' && o[3] != '-'))
        return 0;
    *d = atoi(o + 4);
    if (o[3] == '-')
        *d = -*d;
    return 1;
}

/* A load of HL from memory or a constant that reads no register: its
 * DE form in fmt, and 1; else 0. Each DE form is the same size as its HL
 * form; ld hl,(_sym) is left out because ld de,(_sym) takes an extra
 * prefix byte. */
static int de_form(char *t)
{
    char *s;

    if ((s = after(t, "ld hl,(ix")) != NULL) {
        sprintf(fmt, "ld de,(ix%s", s);
        return 1;
    }
    if ((s = after(t, "lea hl,ix")) != NULL) {
        sprintf(fmt, "lea de,ix%s", s);
        return 1;
    }
    if ((s = after(t, "ld hl,")) != NULL && s[0] != '(') {
        sprintf(fmt, "ld de,%s", s);
        return 1;
    }
    return 0;
}

/* Does instruction t leave register reg, and the local at (ix+d), as they
 * were? Only loads into other registers, and stores to other locals, do. */
static int p_harmless(char *t, int reg, int d)
{
    int rd;
    int wr;
    int e;

    if (!effects(t, &rd, &wr) || (wr & (reg | R_SP)) != 0 || (rd & R_SP) != 0)
        return 0;
    p_split(t);
    if (!p_is(p_mn, "ld") && !p_is(p_mn, "lea"))
        return 0;
    if (p_part(p_o1) && (p_reg(p_o1) & reg) != 0)
        return 0;                       /* a byte of the register itself */
    if (p_o1[0] != '(')
        return 1;
    return ix_off(p_o1, &e) && (e + 2 < d || e > d + 2);     /* another local (3 bytes at most) */
}

/* The rules at record i; 1 if one changed something. */
static int p_rules(int i)
{
    char *t;
    char *s;
    int j;
    int k;
    int m;
    int rd;
    int wr;
    int d1;
    int v;

    t = ins[i].text;
    /* an ex de,hl whose result nothing reads (a store's value swapped back
     * at the end of a statement): it goes */
    if (p_is(t, "ex de,hl") && dead_after(i, R_HL) && dead_after(i, R_DE)) {
        p_gone(i);
        return 1;
    }
    j = p_next(i);
    if (j < 0)
        return 0;
    /* a store to a local, then a load of the same place with nothing
     * between that changes the register or memory: the load goes (only
     * locals: cc2 cannot tell a volatile global) */
    if (after(t, "ld (ix") != NULL) {
        p_split(t);
        if ((p_is(p_o2, "hl") || p_is(p_o2, "e") || p_is(p_o2, "a")) && ix_off(p_o1, &d1)) {
            v = p_reg(p_o2);
            sprintf(fmt, "ld %s,%s", p_o2, p_o1);
            for (k = j, m = 0; k >= 0 && m < 4; k = p_next(k), m++) {
                if (p_is(ins[k].text, fmt)) {
                    p_gone(k);
                    return 1;
                }
                if (!p_harmless(ins[k].text, v, d1))
                    break;
            }
        }
    }
    /* ld (ix+d),hl; ld de,(ix+d) with HL dead after: ex de,hl for the load */
    if (after(t, "ld (ix") != NULL && strcmp(t + strlen(t) - 3, ",hl") == 0) {
        p_split(t);
        sprintf(fmt, "ld de,%s", p_o1);
        if (p_is(ins[j].text, fmt) && dead_after(j, R_HL)) {
            p_set(j, "ex de,hl", 1);
            return 1;
        }
    }
    /* push hl; <a load of HL>; pop de: ex de,hl; <the load> */
    if (p_is(t, "push hl") && (after(ins[j].text, "ld hl,") != NULL || after(ins[j].text, "lea hl,") != NULL)
        && effects(ins[j].text, &rd, &wr) && (rd & (R_HL | R_DE | R_SP)) == 0) {
        k = p_next(j);
        if (k >= 0 && p_is(ins[k].text, "pop de")) {
            p_set(i, "ex de,hl", 1);
            p_gone(k);
            return 1;
        }
    }
    /* ld hl,X; ex de,hl with HL dead after: ld de,X */
    if (p_is(ins[j].text, "ex de,hl") && de_form(t) && dead_after(j, R_HL)) {
        p_set(i, fmt, ins[i].size);
        p_gone(j);
        return 1;
    }
    /* ld de,n; ld (hl),e with DE dead after: ld (hl),n (and (ix+d)) */
    if ((s = after(t, "ld de,")) != NULL && (s[0] == '-' || (s[0] >= '0' && s[0] <= '9'))) {
        v = atoi(s);
        if (v >= -128 && v <= 255 && dead_after(j, R_DE)) {
            if (p_is(ins[j].text, "ld (hl),e")) {
                sprintf(fmt, "ld (hl),%d", v & 255);
                p_set(i, fmt, 2);
                p_gone(j);
                return 1;
            }
            p_split(ins[j].text);
            if (p_is(p_mn, "ld") && p_is(p_o2, "e") && ix_off(p_o1, &d1)) {
                sprintf(fmt, "ld %s,%d", p_o1, v & 255);
                p_set(i, fmt, 4);
                p_gone(j);
                return 1;
            }
        }
    }
    /* ld de,k; or a; sbc hl,de with k 1 to 3, and ld de,k; add hl,de with k
     * -2 to 2, when DE and the flags are dead after: dec hl or inc hl, k times */
    if ((s = after(t, "ld de,")) != NULL && ((s[0] >= '0' && s[0] <= '9' && s[1] == 0)
        || (s[0] == '-' && s[1] >= '1' && s[1] <= '2' && s[2] == 0))) {
        v = atoi(s);
        k = p_next(j);
        if (v >= 1 && v <= 3 && p_is(ins[j].text, "or a") && k >= 0 && p_is(ins[k].text, "sbc hl,de")
            && dead_after(k, R_DE) && dead_after(k, R_F)) {
            p_set(i, "dec hl", 1);
            if (v >= 2)
                p_set(j, "dec hl", 1);
            else
                p_gone(j);
            if (v == 3)
                p_set(k, "dec hl", 1);
            else
                p_gone(k);
            return 1;
        }
        if (v >= -2 && v <= 2 && p_is(ins[j].text, "add hl,de") && dead_after(j, R_DE)
            && dead_after(j, R_F)) {
            if (v == 0) {
                p_gone(i);              /* adding 0 */
                p_gone(j);
                return 1;
            }
            p_set(i, v > 0 ? "inc hl" : "dec hl", 1);
            if (v == 2 || v == -2)
                p_set(j, v > 0 ? "inc hl" : "dec hl", 1);
            else
                p_gone(j);
            return 1;
        }
    }
    /* ex de,hl; add hl,de with DE dead after: add hl,de */
    if (p_is(t, "ex de,hl") && p_is(ins[j].text, "add hl,de") && dead_after(j, R_DE)) {
        p_gone(i);
        return 1;
    }
    /* push af; push hl; <loads that leave A, BC, F and SP alone>; pop bc;
     * pop af: push hl; pop bc; <the loads> (a long into A:UBC) */
    if (p_is(t, "push af") && p_is(ins[j].text, "push hl")) {
        for (k = p_next(j), m = 0; k >= 0 && m < 4; k = p_next(k), m++) {
            if (p_is(ins[k].text, "pop bc"))
                break;
            if (!effects(ins[k].text, &rd, &wr) || ((rd | wr) & (R_A | R_BC | R_F | R_SP)) != 0)
                return 0;
        }
        if (k >= 0 && p_is(ins[k].text, "pop bc")) {
            m = p_next(k);
            if (m >= 0 && p_is(ins[m].text, "pop af")) {
                p_set(i, "push hl", 1);
                p_set(j, "pop bc", 1);
                p_gone(k);
                p_gone(m);
                return 1;
            }
        }
    }
    return 0;
}

/* Sweeps the function with p_rules until a sweep changes nothing (at most
 * 8): one rewrite can make room for another. */
static void peephole(void)
{
    int i;
    int changed;
    int round;

    for (round = 0; round < 8; round++) {
        changed = 0;
        for (i = 0; i < nins; i++)
            if (ins[i].kind == I_TEXT && ins[i].text != NULL && p_rules(i))
                changed = 1;
        if (!changed)
            break;
    }
}

/* Finishes a function: the peephole pass (-O), the jump sizes, the pool
 * layout, then the section: its markers (;;sect, ;;ret, ;;ref,
 * ;;implicit, object_format.md), its label, the code, and the pool. */
static void print_function(void)
{
    int i;
    int s;
    int off;
    int code;
    int t;
    char buf[400];
    struct ins *p;

    if (optimize)
        peephole();
    resolve();
    code = ins_addr[nins];
    off = 0;
    for (s = next_str(-1); s < MAX_STRS; s = next_str(s)) {
        if (str_def[s]) {
            str_off[s] = off;
            off = off + str_len[s] + 1;
        }
    }
    sprintf(buf, ";;sect code %s %s\n;;ret %c\n", func_name, func_vis, func_ret);
    out_str(out_f, buf);
    for (i = 0; i < nrefs; i++) {
        sprintf(buf, ";;ref %s\n", text + refs[i]);
        out_str(out_f, buf);
    }
    for (i = 0; i < nimplicits; i++) {
        sprintf(buf, ";;implicit %s\n", text + implicits[i]);
        out_str(out_f, buf);
    }
    sprintf(buf, "%s:\n", func_name);
    out_str(out_f, buf);
    for (i = 0, p = ins; i < nins; i++, p++) {
        switch (p->kind) {
        case I_TEXT:
            print_line(p->text, i);
            break;
        case I_ASM:
            out_str(out_f, p->text);
            out_str(out_f, "\n");
            break;
        case I_LABEL:
            /* only where a jump or a table names it */
            if (label_used[p->label] && (!offset_branches || func_has_asm)) {
                sprintf(buf, "@L%d:\n", p->label);
                out_str(out_f, buf);
            }
            break;
        case I_JUMP:
            if (p->size == 0)
                break;
            t = label_ins[p->label];
            if (!offset_branches || p->labeled) {
                if (p->cond == C_ALWAYS)
                    sprintf(buf, "%s @L%d", p->size == 2 ? "jr" : "jp", p->label);
                else
                    sprintf(buf, "%s %s,@L%d", p->size == 2 ? "jr" : "jp", cond_names[p->cond], p->label);
            } else {
                /* $ is the jump's own address */
                if (p->cond == C_ALWAYS)
                    sprintf(buf, "%s $%+d", p->size == 2 ? "jr" : "jp", ins_addr[t] - ins_addr[i]);
                else
                    sprintf(buf, "%s %s,$%+d", p->size == 2 ? "jr" : "jp", cond_names[p->cond], ins_addr[t] - ins_addr[i]);
            }
            print_line(buf, i);
            break;
        case I_DL:
        case I_LADDR:
            /* a label's address, which only a table uses */
            if (!offset_branches || func_has_asm)
                sprintf(buf, "%s@L%d", p->kind == I_DL ? "dl " : p->text, p->label);
            else
                sprintf(buf, "%s%s+%d", p->kind == I_DL ? "dl " : p->text, func_name,
                        ins_addr[label_ins[p->label]]);
            print_line(buf, i);
            break;
        case I_POOL:
            /* past the code: the code size plus the entry's offset */
            if (func_has_asm)
                sprintf(buf, "%s@pool+%d", p->text, str_off[p->label]);
            else
                sprintf(buf, "%s%s+%d", p->text, func_name, code + str_off[p->label]);
            print_line(buf, i);
            break;
        }
    }
    if (off > 0) {
        if (func_has_asm)
            out_str(out_f, "@pool:\n");
        for (s = next_str(-1); s < MAX_STRS; s = next_str(s))
            if (str_def[s])
                write_db(text + str_text[s], 1);
    }
    if (nins > peak_ins)
        peak_ins = nins;
    if (ntext > peak_text)
        peak_text = ntext;
}

/* object_format.md section 2: every token of inline assembly that looks
 * like a symbol ('_' followed by identifier characters) gets a ;;ref - a
 * false positive costs nothing, a missing one would drop a needed section.
 * Text after ';' is a comment and is skipped. */
static int ident_char(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static void asm_refs(char *s)
{
    char name[80];
    int n;
    int prev;

    prev = ' ';
    while (*s && *s != ';') {
        if (*s == '_' && !ident_char(prev) && prev != '@') {
            n = 0;
            while (ident_char(*s)) {
                if (n < 79) {
                    name[n] = *s;
                    n++;
                }
                s++;
            }
            name[n] = 0;
            if (n >= 2)
                add_ref(name);
            prev = 'x';
        } else {
            prev = *s;
            s++;
        }
    }
}

/* ---- records ------------------------------------------------------------------- */

static void do_data(int text_base);

/* A switch's cases (sw_val, sw_lab) as a jump table, if one is smaller
 * than the compare chain's 10 bytes a case (R2b); 0 if not. HL holds the
 * value. The span is worked out so that the host (32-bit int) and the
 * Agon (24-bit) decide alike: any span of 2^23 or more is negative on the
 * Agon and far above MAX_TABLE on the host. */
static int jump_table(int cnt, int dflt)
{
    int lo;
    int hi;
    int span;
    int i;
    int v;
    int l;
    int table;
    int cost;

    lo = sw_val[0];
    hi = sw_val[0];
    for (i = 1; i < cnt; i++) {
        if (sw_val[i] < lo)
            lo = sw_val[i];
        if (sw_val[i] > hi)
            hi = sw_val[i];
    }
    span = hi - lo + 1;
    if (span <= 0 || span > MAX_TABLE)
        return 0;
    /* the dispatch code and a 3-byte dl per value, against the compare
     * chain's 10 bytes a case */
    cost = 27 + (lo != 0 ? 5 : 0) + 3 * span;
    if (cost >= 10 * cnt + 4)
        return 0;
    if (lo != 0) {
        sprintf(fmt, "ld de,%d", wrap24(-lo));
        emit(fmt, 4);
        emit("add hl,de", 1);           /* HL = value - lowest case */
    }
    sprintf(fmt, "ld de,%d", span);
    emit(fmt, 4);
    emit("or a", 1);
    /* one unsigned compare checks both ends: a value below lo has wrapped
     * round to a large unsigned number */
    emit("sbc hl,de", 2);               /* carry iff within the table */
    emit_jump(C_NC, dflt);
    emit("add hl,de", 1);
    emit("push hl", 1);
    emit("pop de", 1);
    emit("add hl,hl", 1);
    emit("add hl,de", 1);               /* x3: an entry is a dl */
    table = new_label();
    i = add_ins(I_LADDR, 4);
    ins[i].text = "ld de,";
    ins[i].label = table;
    label_used[table] = 1;
    emit("add hl,de", 1);
    emit("ld hl,(hl)", 2);
    emit("jp (hl)", 1);
    emit_label(table);
    for (v = lo; v < lo + span; v++) {
        l = dflt;
        for (i = 0; i < cnt; i++)
            if (sw_val[i] == v)
                l = sw_lab[i];
        i = add_ins(I_DL, 3);
        ins[i].label = l;
        label_used[l] = 1;
        if (l < MAX_IR_LABELS && l > hi_label)
            hi_label = l;
    }
    return 1;
}

/* WK sym: ;;wref, outside any section, so that it covers every section
 * of the unit (object_format.md 8). */
static void weak_ref(void)
{
    char name[80];
    char buf[100];

    if (ntok != 2)
        internal("malformed WK");
    asm_name(name, tok[1]);
    sprintf(buf, ";;wref %s\n", name);
    out_str(out_f, buf);
}

/* G sym size vis: a bss section, which has no body (ld places it). */
static void do_bss(void)
{
    char name[80];
    char buf[120];

    if (ntok != 4)
        internal("malformed G");
    asm_name(name, tok[1]);
    sprintf(buf, ";;sect bss %s %s %d\n", name, tok[3], num(2));
    out_str(out_f, buf);
}

/* A D inside a function (a block-scope static, or a local aggregate's
 * template): printed at once, the function's own state kept aside. */
static void nested_data(void)
{
    char saved_name[80];
    int saved_refs[MAX_REFS];
    int saved_nrefs;
    int saved_text;
    int i;

    strcpy(saved_name, func_name);
    saved_nrefs = nrefs;
    for (i = 0; i < nrefs; i++)
        saved_refs[i] = refs[i];
    saved_text = ntext;
    do_data(saved_text);
    ntext = saved_text;
    nrefs = saved_nrefs;
    for (i = 0; i < nrefs; i++)
        refs[i] = saved_refs[i];
    strcpy(func_name, saved_name);
}

/* Records that make a D value in a temporary of cc2's: the arithmetic,
 * NEG.d, a post-increment's old value, and a CV to d8; and their Q forms.
 * The test reads the raw line, whose last characters must be the kind
 * suffix. It may count a record that takes no temporary (ST.q, a CV from
 * d8 to d8), which only wastes frame bytes; it must never miss one, or a
 * temporary would lie below the frame. */
static int makes_dtemp(void)
{
    int n;

    n = strlen(line);
    if (n > 2 && line[n - 2] == '.' && line[n - 1] == 'd')
        return strncmp(line, "ADD", 3) == 0 || strncmp(line, "SUB", 3) == 0 || strncmp(line, "MUL", 3) == 0
               || strncmp(line, "DIV", 3) == 0 || strncmp(line, "NEG", 3) == 0 || strncmp(line, "INCPOST", 7) == 0
               || strncmp(line, "DECPOST", 7) == 0;
    if (n > 2 && line[n - 2] == '.' && line[n - 1] == 'q')
        /* every Q operator but the comparisons and INCPRE, DECPRE */
        return !(line[0] == 'E' || strncmp(line, "NE.", 3) == 0 || line[0] == 'L' || line[0] == 'G'
                 || strncmp(line, "INCPRE", 6) == 0 || strncmp(line, "DECPRE", 6) == 0);
    return strncmp(line, "CV ", 3) == 0 && n > 3
           && (strcmp(line + n - 3, " d8") == 0 || strcmp(line + n - 4, " q8s") == 0
               || strcmp(line + n - 4, " q8u") == 0);
}

/* The records that end a statement's tree. */
static int ends_statement(void)
{
    return strcmp(line, "DROP") == 0 || strncmp(line, "RET", 3) == 0 || strncmp(line, "JF ", 3) == 0
           || strncmp(line, "JT ", 3) == 0 || strncmp(line, "SW ", 3) == 0;
}

/* Reads a function's body ahead of translating it, then goes back to the
 * body's start. In a streamed function (F's frame "@", ir_format.md
 * section 4) the frame size, the locals' offsets and where each switch
 * table is come after the body, so they are read here (frame -1: from its
 * FRAME record). For every function it also counts the D temporaries its
 * busiest statement needs, which dtemps is left holding. Returns the frame
 * cc1 laid out. */
static int look_ahead(int frame)
{
    int start;
    int start_line;
    int pos;
    int k;
    int n;
    int i;
    int need;

    start = rd_tell(&in_rd);
    start_line = in_line;
    dtemps = 0;
    need = 0;
    for (i = 0; i < MAX_SWITCHES; i++)
        sw_pos[i] = -1;
    for (;;) {
        pos = rd_tell(&in_rd);
        if (!read_line())
            internal("unexpected end of file inside F");
        if (line[0] == 'E' && line[1] == 0)
            break;
        if (makes_dtemp()) {
            need++;
            if (need > dtemps)
                dtemps = need;
        } else if (ends_statement()) {
            need = 0;
        }
        if (line[0] == 'D' && line[1] == ' ') {
            do {
                if (!read_line())
                    internal("unexpected end of file inside D");
            } while (!(line[0] == 'E' && line[1] == 0));
        } else if (strncmp(line, "ASM ", 4) == 0) {
            n = atoi(line + 4);
            for (i = 0; i < n; i++) {
                if (rd_gets(&in_rd, line, LINE_BUF) == 0)
                    internal("missing ASM line");
                in_line++;
            }
        } else if (strncmp(line, "FRAME ", 6) == 0) {
            split_line();
            if (frame < 0)
                frame = num(1);
        } else if (strncmp(line, "LOC ", 4) == 0) {
            split_line();
            k = num(1);
            if (k < 0 || k >= MAX_LOCALS)
                internal("bad local number");
            loc_off[k] = num(2);
        } else if (strncmp(line, "SWT ", 4) == 0) {
            split_line();
            k = num(1);
            if (k < 0 || k >= MAX_SWITCHES)
                internal("bad switch number");
            sw_pos[k] = pos;
            sw_line[k] = in_line - 1;
        }
    }
    if (frame < 0)
        internal("a streamed function without FRAME");
    rd_seek(&in_rd, start);
    in_line = start_line;
    return frame;
}

/* Does any instruction between the prologue (the first three) and the
 * epilogue (the last three) mention IX? (A name containing "ix" only
 * keeps the frame, which is always safe.) */
static int body_uses_ix(void)
{
    int i;
    struct ins *p;
    char *t;

    for (i = 3, p = ins + 3; i < nins - 3; i++, p++)
        if (p->text != NULL)
            for (t = p->text; *t; t++)
                if (t[0] == 'i' && t[1] == 'x')
                    return 1;
    return 0;
}

/* F: one function, from its F record to its E. The body is read ahead
 * (look_ahead), then translated record by record: expression records build
 * the statement's tree, and the record that ends a statement generates its
 * code. The prologue and epilogue follow abi.md section 5; a function
 * with no frame, no inline asm and no use of IX keeps only the ret. */
static void do_function(void)
{
    int frame;
    int i;
    int n;
    int k;
    int dflt;
    int cnt;
    int swlong;
    int skip;
    int back;
    int back_line;
    struct i32 kval;

    if (ntok != 6)
        internal("malformed F");
    asm_name(func_name, tok[1]);
    strcpy(func_vis, tok[2]);
    /* the return kind from the flags: r (hidden result pointer), l
     * (E:UHL), n (nothing), else an int-sized value */
    func_ret = strchr(tok[5], 'r') ? 'r' : strchr(tok[5], 'l') ? 'l' : strchr(tok[5], 'n') ? 'v' : 'i';
    /* the frame: a number, or "@" for a streamed function, whose layout
     * is read ahead (which reuses tok) */
    frame = look_ahead(strcmp(tok[4], "@") == 0 ? -1 : num(4));
    dtemp_base = frame;
    frame = frame + 8 * dtemps;         /* and below cc1's locals, the D temporaries */
    dtemps = 0;
    for (i = MAX_STRS - ndconst; i < MAX_STRS; i++)
        str_def[i] = 0;
    ndconst = 0;
    nins = 0;
    ntext = 0;
    nrefs = 0;
    nimplicits = 0;
    func_has_asm = 0;
    /* only the IR labels and strings the last function used need clearing
     * (cc2's own labels are set up by new_label) */
    if (!labels_ready)
        hi_label = MAX_IR_LABELS - 1;
    for (i = 0; i <= hi_label; i++) {
        label_ins[i] = -1;
        label_used[i] = 0;
    }
    labels_ready = 1;
    hi_label = 0;
    for (i = 0; i < hi_str; i++)
        str_def[i] = 0;
    hi_str = 0;
    next_label = MAX_IR_LABELS;
    epilogue_label = new_label();
    emit("push ix", 2);
    emit("ld ix,0", 5);
    emit("add ix,sp", 2);
    if (frame > 0) {
        emit_ld_hl_const(-frame);
        emit("add hl,sp", 1);
        emit("ld sp,hl", 1);
    }
    for (;;) {
        if (!read_line())
            internal("unexpected end of file inside F");
        split_line();
        if (ntok == 0)
            continue;
        if (expr_record())
            continue;
        if (is("E")) {
            if (ntree != 0)
                internal("operand stack not empty at E");
            break;
        } else if (is("S")) {
            n = num(1);
            if (n < 0 || n >= MAX_STRS - ndconst)
                limit("string literals and double constants in one function");
            if (ntok < 3 || tok[2][0] != '"')
                internal("malformed S");
            str_text[n] = save_text(tok[2]);
            str_len[n] = string_len(tok[2]);
            str_def[n] = 1;
            if (n >= hi_str)
                hi_str = n + 1;
        } else if (is("L")) {
            n = num(1);
            if (n <= 0 || n >= MAX_IR_LABELS)
                limit("IR labels in one function");
            end_statement();
            emit_label(n);
        } else if (is("J") || is("JF") || is("JT")) {
            n = num(1);
            if (n <= 0 || n >= MAX_IR_LABELS)
                internal("bad label number");
            if (is("J")) {
                end_statement();
                emit_jump(C_ALWAYS, n);
            } else {
                k = pop_tree();
                gen_jump(k, is("JT"), n);
                end_statement();
            }
        } else if (is("RET")) {
            gen(pop_tree());
            end_statement();
            emit_jump(C_ALWAYS, epilogue_label);
        } else if (is("RETB")) {
            /* copy the object to the hidden result object; return its address */
            if (func_ret != 'r')
                internal("RETB in a function without the r flag");
            gen(pop_tree());
            emit("ld de,(ix+6)", 3);
            sprintf(fmt, "ld bc,%d", num(1));
            emit(fmt, 4);
            emit_call("__memcpy");
            end_statement();
            emit_jump(C_ALWAYS, epilogue_label);
        } else if (is("RETV")) {
            end_statement();
            emit_jump(C_ALWAYS, epilogue_label);
        } else if (is("DROP")) {
            void_node = pop_tree();
            gen(void_node);
            void_node = -1;
            if (optimize)
                add_ins(I_STMT, 0);     /* for the peephole pass: every register is dead here */
            end_statement();
        } else if (is("SW")) {
            /* the value in HL (or E:UHL), then a jump table or a chain of
             * compares, one per case, and a jump to the default */
            n = pop_tree();
            swlong = nodes[n].kind == K_L;
            gen(n);
            end_statement();
            back = -1;
            back_line = 0;
            if (ntok >= 2 && tok[1][0] == '@') {
                /* the table is after the body: read it there, then come back */
                k = atoi(tok[1] + 1);
                if (k < 0 || k >= MAX_SWITCHES || sw_pos[k] < 0)
                    internal("SW of an unknown switch table");
                back = rd_tell(&in_rd);
                back_line = in_line;
                rd_seek(&in_rd, sw_pos[k]);
                in_line = sw_line[k];
                if (!read_line())
                    internal("missing SWT");
                split_line();
                if (!is("SWT") || ntok != 4)
                    internal("expected SWT");
                dflt = num(2);
                cnt = num(3);
            } else {
                dflt = num(1);
                cnt = num(2);
            }
            if (!swlong && cnt >= 4 && cnt <= MAX_SW_CASES) {
                for (i = 0; i < cnt; i++) {
                    if (!read_line())
                        internal("missing K");
                    split_line();
                    if (!is("K") || ntok != 3)
                        internal("expected K");
                    sw_val[i] = num(1);
                    sw_lab[i] = num(2);
                    if (sw_lab[i] <= 0 || sw_lab[i] >= MAX_IR_LABELS)
                        internal("bad label number");
                }
                if (dflt <= 0 || dflt >= MAX_IR_LABELS)
                    internal("bad label number");
                if (!jump_table(cnt, dflt)) {
                    for (i = 0; i < cnt; i++) {
                        sprintf(fmt, "ld de,%d", sw_val[i]);
                        emit(fmt, 4);
                        emit("or a", 1);
                        emit("sbc hl,de", 2);
                        emit("add hl,de", 1);   /* restores HL; Z survives */
                        emit_jump(C_Z, sw_lab[i]);
                    }
                    emit_jump(C_ALWAYS, dflt);
                }
                if (back >= 0) {
                    rd_seek(&in_rd, back);
                    in_line = back_line;
                }
                continue;
            }
            for (i = 0; i < cnt; i++) {
                if (!read_line())
                    internal("missing K");
                split_line();
                if (!is("K") || ntok != 3)
                    internal("expected K");
                k = num(2);
                if (k <= 0 || k >= MAX_IR_LABELS)
                    internal("bad label number");
                if (swlong) {
                    /* E:UHL against a 32-bit case: the low 24 bits, then E */
                    if (!i32_parse(&kval, tok[1]))
                        internal("bad K value");
                    sprintf(fmt, "ld bc,%d", i32_low24(&kval));
                    emit(fmt, 4);
                    emit("or a", 1);
                    emit("sbc hl,bc", 2);
                    emit("add hl,bc", 1);   /* restores HL; Z survives */
                    skip = new_label();
                    emit_jump(C_NZ, skip);
                    emit("ld a,e", 1);
                    sprintf(fmt, "cp %d", i32_high8(&kval));
                    emit(fmt, 2);
                    emit_jump(C_Z, k);
                    emit_label(skip);
                    continue;
                }
                sprintf(fmt, "ld de,%d", num(1));
                emit(fmt, 4);
                emit("or a", 1);
                emit("sbc hl,de", 2);
                emit("add hl,de", 1);   /* restores HL; Z survives */
                emit_jump(C_Z, k);
            }
            if (dflt <= 0 || dflt >= MAX_IR_LABELS)
                internal("bad label number");
            emit_jump(C_ALWAYS, dflt);
            if (back >= 0) {
                rd_seek(&in_rd, back);
                in_line = back_line;
            }
        } else if (is("SWT")) {
            /* a switch table, already used at its SW: skip it */
            cnt = num(3);
            for (i = 0; i < cnt; i++)
                if (!read_line())
                    internal("missing K");
        } else if (is("FRAME") || is("LOC")) {
            /* read ahead already */
        } else if (is("WK")) {
            weak_ref();                 /* outside any section, like a nested G */
        } else if (is("R")) {
            /* a reference no expression shows */
            if (ntok != 2)
                internal("malformed R");
            asm_name(fmt, tok[1]);
            add_ref(fmt);
        } else if (is("G")) {
            /* a block-scope static: its section is printed now, before the
             * function's, as for any object defined before it */
            do_bss();
        } else if (is("D")) {
            nested_data();
        } else if (is("ASM")) {
            end_statement();
            cnt = num(1);
            for (i = 0; i < cnt; i++) {
                if (rd_gets(&in_rd, line, LINE_BUF) == 0)
                    internal("missing ASM line");
                in_line++;
                n = strlen(line);
                while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
                    n--;
                    line[n] = 0;
                }
                k = add_ins(I_ASM, 0);
                ins[k].text = text + save_text(line);
                func_has_asm = 1;
                asm_refs(line);
            }
        } else {
            internal("unknown record");
        }
    }
    emit_label(epilogue_label);
    emit("ld sp,ix", 2);
    emit("pop ix", 2);
    emit("ret", 1);
    if (frame == 0 && !func_has_asm && !body_uses_ix()) {
        /* no frame needed (R2b): the prologue and all of the epilogue but ret go */
        ins[0].kind = I_GONE;
        ins[1].kind = I_GONE;
        ins[2].kind = I_GONE;
        ins[nins - 3].kind = I_GONE;
        ins[nins - 2].kind = I_GONE;
        ins[0].size = 0;
        ins[1].size = 0;
        ins[2].size = 0;
        ins[nins - 3].size = 0;
        ins[nins - 2].size = 0;
    }
    print_function();
}

/* One data item of a D object as its line of assembly, printed; a string
 * object (SO) is printed as its own section, first, in the first pass. */
static void data_item(int print)
{
    char buf[400];
    struct i32 qval;
    int i;

    if (is("B") || is("W") || is("T")) {
        sprintf(buf, "\t%s %d\n", is("B") ? "db" : is("W") ? "dw" : "dl", num(1));
    } else if (is("Q")) {
        /* four bytes: a long, or a float's bits (IR 2) */
        if (ntok < 2 || !i32_parse(&qval, tok[1]))
            internal("bad Q item");
        sprintf(buf, "\tdb %d,%d,%d,%d\n", qval.lo & 255, qval.lo >> 8, qval.hi & 255, qval.hi >> 8);
    } else if (is("H")) {
        /* a double's or long long's eight bytes, in memory order */
        if (ntok < 2 || strlen(tok[1]) != 16)
            internal("bad H item");
        strcpy(buf, "\tdb ");
        for (i = 0; i < 16; i = i + 2)
            sprintf(buf + strlen(buf), i < 14 ? "%d," : "%d\n", hexval(tok[1][i]) * 16 + hexval(tok[1][i + 1]));
    } else if (is("A")) {
        asm_name(fmt, tok[1]);
        if (!print) {
            add_ref(fmt);
            return;
        }
        sprintf(buf, "\tdl %s+%d\n", fmt, num(2));
    } else if (is("S")) {
        if (ntok < 2 || tok[1][0] != '"')
            internal("malformed S item");
        if (print)
            write_db(tok[1], 0);
        return;
    } else if (is("SO")) {
        /* a string object defined beside this one: printed first, as
         * its own data section, with its NUL */
        if (ntok < 3 || tok[2][0] != '"')
            internal("malformed SO item");
        if (print)
            return;
        asm_name(fmt, tok[1]);
        sprintf(buf, ";;sect data %s s\n%s:\n", fmt, fmt);
        out_str(out_f, buf);
        write_db(tok[2], 0);
        out_str(out_f, "\tdb 0\n");
        return;
    } else if (is("Z")) {
        sprintf(buf, "\tblkb %d,0\n", num(1));
    } else {
        internal("unknown data item");
    }
    if (print)
        out_str(out_f, buf);
}

/* A D object, in two passes over its items: the first collects its
 * references (for ;;ref, which comes before the items) and prints its
 * string objects; the second, read again from the IR, prints the items. */
static void do_data(int text_base)
{
    char name[80];
    char vis[4];
    char buf[400];
    int i;
    int start;
    int start_line;
    int pass;

    if (ntok != 3)
        internal("malformed D");
    asm_name(name, tok[1]);
    strcpy(vis, tok[2]);
    ntext = text_base;
    nrefs = 0;
    strcpy(func_name, name);
    start = rd_tell(&in_rd);
    start_line = in_line;
    for (pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            sprintf(buf, ";;sect data %s %s\n", name, vis);
            out_str(out_f, buf);
            for (i = 0; i < nrefs; i++) {
                sprintf(buf, ";;ref %s\n", text + refs[i]);
                out_str(out_f, buf);
            }
            sprintf(buf, "%s:\n", name);
            out_str(out_f, buf);
            rd_seek(&in_rd, start);
            in_line = start_line;
        }
        for (;;) {
            if (!read_line())
                internal("unexpected end of file inside D");
            split_line();
            if (is("E"))
                break;
            data_item(pass);
        }
    }
}

/* The whole unit: the IR 2 header, U, then G, D, WK and F records, each
 * printed as it is read, between ;;agonc-object 1 and ;;end. */
static void translate(void)
{
    char buf[400];

    in_line = 0;
    if (!read_line())
        internal("empty IR file");
    split_line();
    if (!is("IR") || ntok != 2 || strcmp(tok[1], "2") != 0)
        internal("not an IR 2 file");
    out_str(out_f, ";;agonc-object 1\n");
    while (read_line()) {
        split_line();
        if (is("U")) {
            if (ntok != 3)
                internal("malformed U");
            strcpy(unit_name, tok[1]);
            strcpy(unit_id, tok[2]);
            sprintf(buf, ";;unit %s %s\n", unit_name, unit_id);
            out_str(out_f, buf);
        } else if (unit_id[0] == 0) {
            internal("no U record");
        } else if (is("G")) {
            do_bss();
        } else if (is("D")) {
            do_data(0);
        } else if (is("WK")) {
            weak_ref();
        } else if (is("F")) {
            do_function();
        } else {
            internal("unexpected record at file scope");
        }
    }
    out_str(out_f, ";;end\n");
}

/* The tables by binary operator: the IR names, and the helper each kind
 * calls. An empty helper is an operator made inline; GT and GE call the
 * LT and LE helpers with the operands exchanged. */
static void init_tables(void)
{
    bin_names[B_ADD] = "ADD";   bin_helpers[B_ADD] = "";
    bin_names[B_SUB] = "SUB";   bin_helpers[B_SUB] = "";
    bin_names[B_MUL] = "MUL";   bin_helpers[B_MUL] = "__imul";
    bin_names[B_DIVS] = "DIVS"; bin_helpers[B_DIVS] = "__idivs";
    bin_names[B_DIVU] = "DIVU"; bin_helpers[B_DIVU] = "__idivu";
    bin_names[B_REMS] = "REMS"; bin_helpers[B_REMS] = "__irems";
    bin_names[B_REMU] = "REMU"; bin_helpers[B_REMU] = "__iremu";
    bin_names[B_SHL] = "SHL";   bin_helpers[B_SHL] = "__ishl";
    bin_names[B_SHRS] = "SHRS"; bin_helpers[B_SHRS] = "__ishrs";
    bin_names[B_SHRU] = "SHRU"; bin_helpers[B_SHRU] = "__ishru";
    bin_names[B_AND] = "AND";   bin_helpers[B_AND] = "__iand";
    bin_names[B_OR] = "OR";     bin_helpers[B_OR] = "__ior";
    bin_names[B_XOR] = "XOR";   bin_helpers[B_XOR] = "__ixor";
    bin_names[B_EQ] = "EQ";     bin_helpers[B_EQ] = "";
    bin_names[B_NE] = "NE";     bin_helpers[B_NE] = "";
    bin_names[B_LTS] = "LTS";   bin_helpers[B_LTS] = "__ilts";
    bin_names[B_LTU] = "LTU";   bin_helpers[B_LTU] = "";
    bin_names[B_LES] = "LES";   bin_helpers[B_LES] = "__iles";
    bin_names[B_LEU] = "LEU";   bin_helpers[B_LEU] = "";
    bin_names[B_GTS] = "GTS";   bin_helpers[B_GTS] = "__ilts";
    bin_names[B_GTU] = "GTU";   bin_helpers[B_GTU] = "";
    bin_names[B_GES] = "GES";   bin_helpers[B_GES] = "__iles";
    bin_names[B_GEU] = "GEU";   bin_helpers[B_GEU] = "";
    lhelper[B_ADD] = "";                /* inline */
    lhelper[B_SUB] = "__lsub";
    lhelper[B_MUL] = "__lmul";
    lhelper[B_DIVS] = "__ldivs";
    lhelper[B_DIVU] = "__ldivu";
    lhelper[B_REMS] = "__lrems";
    lhelper[B_REMU] = "__lremu";
    lhelper[B_SHL] = "__lshl";
    lhelper[B_SHRS] = "__lshrs";
    lhelper[B_SHRU] = "__lshru";
    lhelper[B_AND] = "__land";
    lhelper[B_OR] = "__lor";
    lhelper[B_XOR] = "__lxor";
    lhelper[B_EQ] = "__leq";
    lhelper[B_NE] = "";                 /* __leq, inverted */
    lhelper[B_LTS] = "__llts";
    lhelper[B_LTU] = "__lltu";
    lhelper[B_LES] = "__lles";
    lhelper[B_LEU] = "__lleu";
    lhelper[B_GTS] = "__llts";          /* operands swapped */
    lhelper[B_GTU] = "__lltu";
    lhelper[B_GES] = "__lles";
    lhelper[B_GEU] = "__lleu";
    fhelper[B_ADD] = "___fadd";
    fhelper[B_SUB] = "___fsub";
    fhelper[B_MUL] = "___fmul";
    fhelper[B_DIVS] = "___fdiv";
    fhelper[B_EQ] = "___feq";
    fhelper[B_NE] = "___fne";
    fhelper[B_LTS] = "___flt";
    fhelper[B_LES] = "___fle";
    fhelper[B_GTS] = "___fgt";
    fhelper[B_GES] = "___fge";
    dhelper[B_ADD] = "___dadd";
    dhelper[B_SUB] = "___dsub";
    dhelper[B_MUL] = "___dmul";
    dhelper[B_DIVS] = "___ddiv";
    dhelper[B_EQ] = "___deq";
    dhelper[B_NE] = "___dne";
    dhelper[B_LTS] = "___dlt";
    dhelper[B_LES] = "___dle";
    dhelper[B_GTS] = "___dgt";
    dhelper[B_GES] = "___dge";
    cond_names[C_ALWAYS] = "";
    cond_names[C_Z] = "z";
    cond_names[C_NZ] = "nz";
    cond_names[C_C] = "c";
    cond_names[C_NC] = "nc";
    cond_names[C_M] = "m";
    cond_names[C_P] = "p";
    cond_names[C_PE] = "pe";
    cond_names[C_PO] = "po";
}

/* Options anywhere, then the input and output paths in that order. Any
 * failure exits with status 200, the status the driver also gives for a
 * failure (driver.md section 6). */
static char *args[MAX_ARGS];

int main(int argc, char **argv)
{
    int n;
    int i;

    init_tables();
    n = expand_args("cc2", argc, argv, args);
    if (n < 0)
        return 200;
    in_path = NULL;
    out_path = NULL;
    offset_branches = 1;
    for (i = 0; i < n; i++) {
        if (strcmp(args[i], "-O") == 0)
            optimize = 1;
        else if (strcmp(args[i], "-fno-offset-branches") == 0)
            offset_branches = 0;
        else if (strcmp(args[i], "-a") == 0)
            annotate = 1;
        else if (strcmp(args[i], "-v") == 0)
            verbose = 1;
        else if (args[i][0] == '-') {
            fprintf(stderr, "cc2: unknown option %s\n", args[i]);
            return 200;
        } else if (in_path == NULL)
            in_path = args[i];
        else if (out_path == NULL)
            out_path = args[i];
        else {
            fprintf(stderr, "cc2: too many file names\n");
            return 200;
        }
    }
    if (in_path == NULL || out_path == NULL) {
        fprintf(stderr, "usage: cc2 in.ir out.s [-fno-offset-branches] [-a] [-v]\n");
        return 200;
    }
    if (!rd_open(&in_rd, in_path)) {
        fprintf(stderr, "cc2: cannot open %s\n", in_path);
        return 200;
    }
    out_f = fopen(out_path, "wb");
    if (out_f == NULL) {
        fprintf(stderr, "cc2: cannot create %s\n", out_path);
        return 200;
    }
    translate();
    fclose(out_f);
    out_f = NULL;
    rd_close(&in_rd);
    if (verbose)
        printf("cc2: peaks: instructions %d/%d, text %d/%d, nodes %d/%d\n",
               peak_ins, MAX_INS, peak_text, TEXT_SIZE, peak_nodes, MAX_NODES);
    return 0;
}
