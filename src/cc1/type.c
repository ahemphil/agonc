/* type.c - types (deduplicated, so equal types have equal indices) and the
 * global and local symbol tables.
 *
 * Used by stmt.c, which declares things, and expr.c, which looks them up
 * and types expressions; emit.c reads the locals to lay out the frame.
 *
 * Types: types[] holds every type the unit has used. A constructor
 * (ptr_to, array_of, func_type, qualify) first searches for an equal
 * entry and returns it, so a type is made once and same_type is ==. This
 * is hash-consing without the hash: a linear search, which is cheap at
 * this table's size. The basic types come first, at fixed indices (T_*).
 * A struct type is the exception to structural equality: each tag gets
 * its own TY_STRUCT entry, whose base is the tag, so two structs with the
 * same members are still different types, as C requires. Function
 * parameter lists live in plist[], each function type owning a run.
 *
 * Symbols: globals[] and locals[] (cc1.h), with global_of, local_of and
 * tag_of mapping a name number straight to its visible symbol. Scopes
 * work by shadowing: a declaration that hides a name remembers the symbol
 * it hid, and leaving the scope restores it. A closed block's locals stay
 * in the table until the function ends, since the frame layout needs
 * them all; only their names stop being visible. (drop_locals, for a
 * parameter list's names, is the one place locals are taken out early.)
 *
 * Struct layout follows abi.md 2: members in order with no padding (the
 * eZ80 has no alignment rules), bit-fields packed into bytes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cc1.h"

struct type types[MAX_TYPES];
static int ntypes;
int plist[MAX_PLIST];
static int nplist;

struct gsym globals[MAX_GLOBALS];
int nglobals;
struct lsym locals[MAX_LOCALS];
int nlocals;
static int peak_locals;
int nparams_cur;
int scope_depth;
int nblocks;
int block_parent[MAX_BLOCKS];
static int cur_block;           /* the block new locals belong to */
static int local_mark[MAX_DEPTH];       /* per open scope: nlocals, ntags and the block on entry */
static int tag_mark[MAX_DEPTH];
static int block_mark[MAX_DEPTH];

struct tag tags[MAX_TAGS];
static int ntags;
struct member members[MAX_MEMBERS];
static int nmembers;

static int global_of[MAX_NAMES];        /* name -> global symbol, or -1 */
static int local_of[MAX_NAMES];         /* name -> local symbol, or -1 */
static int tag_of[MAX_NAMES];           /* name -> tag, or -1 */

/* A fixed table is full: fatal, and the message says which. */
static void limit(char *what)
{
    fprintf(stderr, "%s:%d: error: too many %s (a cc1 table limit; raise it in cc1.h)\n", cur_file, tok_line, what);
    give_up();
}

/* ---- types ----------------------------------------------------------------------- */

/* A type, qualified by q: the existing one if it is already in the table.
 * Size is not compared: kind, base and count decide it. Function types
 * never match here, since their parameter lists must be compared too
 * (func_type does that). */
static int new_qtype(int kind, int base, int count, int size, int q)
{
    int i;

    for (i = 0; i < ntypes; i++)
        if (types[i].kind == kind && types[i].base == base && types[i].count == count && types[i].qual == q
            && kind != TY_FUNC)
            return i;
    if (ntypes >= MAX_TYPES)
        limit("types");
    types[ntypes].kind = kind;
    types[ntypes].qual = q;
    types[ntypes].base = base;
    types[ntypes].count = count;
    types[ntypes].size = size;
    types[ntypes].params = 0;
    types[ntypes].nparams = 0;
    types[ntypes].variadic = 0;
    ntypes++;
    return ntypes - 1;
}

static int new_type(int kind, int base, int count, int size)
{
    return new_qtype(kind, base, count, size, 0);
}

/* The basic types, in the order of the T_* numbers (cc1.h), with their
 * sizes from abi.md 2; and every name mapped to no symbol. */
void type_init(void)
{
    int i;

    ntypes = 0;
    new_type(TY_VOID, -1, 0, 0);
    new_type(TY_CHAR, -1, 0, 1);
    new_type(TY_UCHAR, -1, 0, 1);
    new_type(TY_INT, -1, 0, 3);
    new_type(TY_UINT, -1, 0, 3);
    new_type(TY_SCHAR, -1, 0, 1);
    new_type(TY_SHORT, -1, 0, 2);
    new_type(TY_USHORT, -1, 0, 2);
    new_type(TY_LONG, -1, 0, 4);
    new_type(TY_ULONG, -1, 0, 4);
    new_type(TY_FLOAT, -1, 0, 4);
    new_type(TY_DOUBLE, -1, 0, 8);
    new_type(TY_LDOUBLE, -1, 0, 8);
    new_type(TY_LLONG, -1, 0, 8);
    new_type(TY_ULLONG, -1, 0, 8);
    for (i = 0; i < MAX_NAMES; i++) {
        global_of[i] = -1;
        local_of[i] = -1;
        tag_of[i] = -1;
    }
}

int is_struct(int t)
{
    return types[t].kind == TY_STRUCT;
}

/* ---- tags and members ------------------------------------------------------------ */

int find_tag(int name)
{
    return tag_of[name];
}

/* A new tag; a struct tag gets its own (so far incomplete) type. */
int add_tag(int name, int is_enum, int local)
{
    if (ntags >= MAX_TAGS)
        limit("struct and enum tags");
    tags[ntags].name = name;
    tags[ntags].is_enum = is_enum;
    tags[ntags].is_union = 0;
    tags[ntags].fill = 0;
    tags[ntags].bits = 0;
    tags[ntags].type = is_enum ? T_INT : new_type(TY_STRUCT, ntags, 0, 0);
    tags[ntags].members = -1;
    tags[ntags].complete = is_enum;
    tags[ntags].local = local;
    tags[ntags].shadowed = -1;
    if (name >= 0) {
        tags[ntags].shadowed = tag_of[name];
        tag_of[name] = ntags;
    }
    ntags++;
    return ntags - 1;
}

/* Append an ordinary member to an incomplete struct; returns the member. */
int add_member(int tag, int name, int type)
{
    return add_field(tag, name, type, -1);
}

/* A member of tag, laid out as abi.md 2 says: width -1 for an ordinary
 * member, else a bit-field's width. Bit-fields fill a byte from its least
 * significant bit; one of 8 bits or fewer that does not fit in what the
 * current byte has left starts the next byte, a wider one always starts a
 * byte, a width of 0 only moves to the next byte, and an ordinary member
 * after bit-fields starts the next byte. In a union every member is at
 * offset 0 (a bit-field at bit 0) and fill tracks the largest. An unnamed
 * bit-field (name -1) only takes space. Returns the member, or -1 if none
 * was added. */
int add_field(int tag, int name, int type, int width)
{
    int m;
    int last;
    int off;
    int bit;
    int n;

    /* the position: tags[tag].fill is the next free byte and .bits the
     * bits already used in the byte before it (0: that byte is full or
     * holds no bit-fields) */
    bit = 0;
    if (tags[tag].is_union) {
        off = 0;
        n = width < 0 ? type_size(type) : (width + 7) / 8;
        if (n > tags[tag].fill)
            tags[tag].fill = n;
    } else if (width < 0) {
        off = tags[tag].fill;
        tags[tag].fill = off + type_size(type);
        tags[tag].bits = 0;
    } else if (width == 0) {
        tags[tag].bits = 0;
        return -1;
    } else if (width <= 8 && tags[tag].bits > 0 && tags[tag].bits + width <= 8) {
        /* fits in what the last byte has left */
        off = tags[tag].fill - 1;
        bit = tags[tag].bits;
        tags[tag].bits = bit + width;
    } else {
        /* a fresh byte, and as many more as the width needs; a width
         * that is not a whole number of bytes leaves its last byte open */
        off = tags[tag].fill;
        tags[tag].fill = off + (width + 7) / 8;
        tags[tag].bits = width % 8;
    }
    if (name < 0)
        return -1;
    /* append to the end of the tag's member list */
    if (nmembers >= MAX_MEMBERS)
        limit("struct members");
    last = -1;
    for (m = tags[tag].members; m >= 0; m = members[m].next)
        last = m;
    members[nmembers].name = name;
    members[nmembers].type = type;
    members[nmembers].offset = off;
    members[nmembers].bit = bit;
    members[nmembers].width = width < 0 ? 0 : width;
    members[nmembers].next = -1;
    if (last < 0)
        tags[tag].members = nmembers;
    else
        members[last].next = nmembers;
    nmembers++;
    return nmembers - 1;
}

/* tag's member called name, or -1 (a walk along the member list) */
int find_member(int tag, int name)
{
    int m;

    for (m = tags[tag].members; m >= 0; m = members[m].next)
        if (members[m].name == name)
            return m;
    return -1;
}

/* A struct's size is the sum of its members' (abi.md: no padding); a
 * union's, the largest member's. Both are what add_field left in fill.
 * The size goes into the tag's own type; qualified copies made earlier
 * read it through type_size. */
void complete_struct(int tag)
{
    int size;

    size = tags[tag].fill;
    types[tags[tag].type].size = size;
    tags[tag].complete = 1;
}

/* Whether name, as visible here, is a typedef: the type it names, or -1.
 * The parser asks this to tell a declaration from an expression, the one
 * place C's grammar depends on the symbol table. */
int typedef_type(int name)
{
    int g;

    if (local_of[name] >= 0)             /* a local typedef, or a local hiding the typedef */
        return locals[local_of[name]].kind == LK_TYPEDEF ? locals[local_of[name]].type : -1;
    g = global_of[name];
    if (g < 0 || globals[g].kind != SK_TYPEDEF)
        return -1;
    return globals[g].type;
}

/* A type distinct from every real one: the placeholder for a nested
 * declarator. In "int (*f)(void)" the inner "*f" is parsed first, as a
 * pointer to the hole; once the outer suffixes give the real type, stmt.c
 * puts it in the hole's place (subst). Deduplication makes every hole the
 * same type, which is enough: the type being built has one unfilled
 * place at a time. */
int make_hole(void)
{
    return new_type(TY_HOLE, -1, 0, 0);
}

/* t -> the type "pointer to t", once made (0, void's number, means not yet):
 * decay makes one for every array or function value, and the table never
 * shrinks, so the answer never changes. */
static int ptr_type[MAX_TYPES];

/* "pointer to t", from the cache when it can (a negative t, which has no
 * cache slot, takes the table search) */
int ptr_to(int t)
{
    if (t < 0)
        return new_type(TY_PTR, t, 0, 3);
    if (ptr_type[t] == 0)
        ptr_type[t] = new_type(TY_PTR, t, 0, 3);
    return ptr_type[t];
}

/* "array of count t"; count -1 (unknown, as in "int a[]") gives size 0 */
int array_of(int t, int count)
{
    return new_type(TY_ARRAY, t, count, count < 0 ? 0 : type_size(t) * count);
}

/* "function returning ret" with n parameter types (n -1: no prototype),
 * deduplicated by comparing the whole signature; a new one copies its
 * parameter types into plist. Size 0: a function is not an object. */
int func_type(int ret, int *params, int n, int variadic)
{
    int i;
    int k;

    for (i = 0; i < ntypes; i++) {
        if (types[i].kind == TY_FUNC && types[i].base == ret && types[i].nparams == n
            && types[i].variadic == variadic) {
            for (k = 0; k < n; k++)
                if (plist[types[i].params + k] != params[k])
                    break;
            if (k == n)
                return i;
        }
    }
    if (ntypes >= MAX_TYPES)
        limit("types");
    if (nplist + n > MAX_PLIST)
        limit("parameter types");
    types[ntypes].kind = TY_FUNC;
    types[ntypes].base = ret;
    types[ntypes].count = 0;
    types[ntypes].size = 0;
    types[ntypes].params = nplist;
    types[ntypes].nparams = n;
    types[ntypes].variadic = variadic;
    for (k = 0; k < n; k++) {
        plist[nplist] = params[k];
        nplist++;
    }
    ntypes++;
    return ntypes - 1;
}

/* sizeof t in bytes; 0 for void, a function, an incomplete struct or an
 * array of unknown size */
int type_size(int t)
{
    if (types[t].kind == TY_STRUCT)     /* a qualified copy's size may predate completion */
        return types[tags[types[t].base].type].size;
    return types[t].size;
}

/* ---- type predicates --------------------------------------------------------------- */

/* the integer types (an enum's type is int, so enums count) */
int is_integer(int t)
{
    int k;

    k = types[t].kind;
    return k == TY_CHAR || k == TY_UCHAR || k == TY_INT || k == TY_UINT || k == TY_SCHAR || k == TY_SHORT
        || k == TY_USHORT || k == TY_LONG || k == TY_ULONG || k == TY_LLONG || k == TY_ULLONG;
}

/* unsigned arithmetic for t: the unsigned integers and pointers (an
 * address compares as an unsigned 24-bit value) */
int is_unsigned(int t)
{
    int k;

    k = types[t].kind;
    return k == TY_UINT || k == TY_UCHAR || k == TY_PTR || k == TY_USHORT || k == TY_ULONG || k == TY_ULLONG;
}

/* long or unsigned long: a 32-bit L value (not long long) */
int is_long(int t)
{
    return types[t].kind == TY_LONG || types[t].kind == TY_ULONG;
}

int is_void(int t)
{
    return types[t].kind == TY_VOID;
}

int is_floating(int t)
{
    return types[t].kind == TY_FLOAT || types[t].kind == TY_DOUBLE || types[t].kind == TY_LDOUBLE;
}

/* double or long double: an 8-byte value handled by its address (abi.md 3) */
int is_double(int t)
{
    return types[t].kind == TY_DOUBLE || types[t].kind == TY_LDOUBLE;
}

/* long long or unsigned long long */
int is_llong(int t)
{
    return types[t].kind == TY_LLONG || types[t].kind == TY_ULLONG;
}

/* A value of 8 bytes, which is handled by its address as a struct is
 * (abi.md 3): a double, a long double or a long long. */
int is_mem8(int t)
{
    return is_double(t) || is_llong(t);
}

int is_arith(int t)
{
    return is_integer(t) || is_floating(t);
}

/* Argument slots (abi.md 4), 3 bytes each: a long or float takes two, a
 * double or a long long three, a struct ceil(size / 3), everything else
 * one. A scalar local takes as many in the frame (abi.md 5). */
int slots(int t)
{
    if (is_struct(t))
        return (type_size(t) + 2) / 3;
    if (is_mem8(t))
        return 3;
    return is_long(t) || types[t].kind == TY_FLOAT ? 2 : 1;
}

/* t with qualifiers q added. Qualifying an array qualifies its elements (C89
 * 3.5.3); a function takes none. A qualified type is a separate entry
 * from the unqualified one (qual is part of the identity), deduplicated
 * like any other. */
int qualify(int t, int q)
{
    if (q == 0 || (types[t].qual & q) == q || types[t].kind == TY_FUNC)
        return t;
    if (types[t].kind == TY_ARRAY)
        return array_of(qualify(types[t].base, q), types[t].count);
    return new_qtype(types[t].kind, types[t].base, types[t].count, types[t].size, types[t].qual | q);
}

/* t without its own qualifiers (a value's type; C89 3.2.2.1). */
int unqual(int t)
{
    if (types[t].qual == 0)
        return t;
    return new_qtype(types[t].kind, types[t].base, types[t].count, types[t].size, 0);
}

int is_pointer(int t)
{
    return types[t].kind == TY_PTR;
}

int is_scalar(int t)
{
    return is_integer(t) || is_floating(t) || is_pointer(t);
}

/* Arrays decay to pointers to their element, functions to pointers to themselves. */
int decay(int t)
{
    if (types[t].kind == TY_ARRAY)
        return ptr_to(types[t].base);
    if (types[t].kind == TY_FUNC)
        return ptr_to(t);
    return t;
}

/* The integer promotions: every type narrower than int promotes to int,
 * since int (24 bits) holds all their values. */
int promote(int t)
{
    int k;

    k = types[t].kind;
    if (k == TY_CHAR || k == TY_UCHAR || k == TY_SCHAR || k == TY_SHORT || k == TY_USHORT)
        return T_INT;
    return unqual(t);
}

/* deduplication makes type identity index equality */
int same_type(int a, int b)
{
    return a == b;
}

/* ---- symbols ----------------------------------------------------------------------- */

int find_global(int name)
{
    return global_of[name];
}

/* A new global symbol, which becomes the one name refers to at file
 * scope. Globals are never removed; the caller checks redeclarations. */
int add_global(int name, int kind, int type, int sclass)
{
    if (nglobals >= MAX_GLOBALS)
        limit("global symbols");
    globals[nglobals].name = name;
    globals[nglobals].kind = kind;
    globals[nglobals].type = type;
    globals[nglobals].sclass = sclass;
    globals[nglobals].defined = 0;
    globals[nglobals].used = 0;
    globals[nglobals].line = tok_line;
    globals[nglobals].val = 0;
    global_of[name] = nglobals;
    globals[nglobals].implicit = 0;
    globals[nglobals].tentative = 0;
    nglobals++;
    return nglobals - 1;
}

int find_local(int name)
{
    return local_of[name];
}

/* A new local, visible from here on as name (hiding any outer one). A
 * parameter's frame offset is known at once: the slots start at ix+6,
 * after the saved IX and the return address (abi.md 5), and nparams_cur
 * already counts a hidden result pointer when there is one. A variable's
 * offset waits for func_end (emit.c), which sees the whole function. */
int add_local(int name, int type, int param)
{
    if (nlocals >= MAX_LOCALS)
        limit("locals in one function");
    locals[nlocals].name = name;
    locals[nlocals].kind = LK_VAR;
    locals[nlocals].type = type;
    locals[nlocals].param = param;
    locals[nlocals].temp = 0;
    locals[nlocals].offset = param ? 6 + 3 * nparams_cur : 0;
    if (param)
        nparams_cur = nparams_cur + slots(type);   /* a long takes two slots (abi.md 4) */
    locals[nlocals].shadowed = local_of[name];
    locals[nlocals].depth = scope_depth;
    locals[nlocals].block = cur_block;
    local_of[name] = nlocals;
    nlocals++;
    return nlocals - 1;
}

/* A block-scope name for global symbol g (an extern or a static). */
int add_local_global(int name, int g)
{
    int l;

    l = add_local(name, globals[g].type, 0);
    locals[l].kind = LK_GLOBAL;
    locals[l].offset = g;
    return l;
}

/* A block-scope typedef: a local that names a type and takes no space. */
int add_local_typedef(int name, int t)
{
    int l;

    l = add_local(name, t, 0);
    locals[l].kind = LK_TYPEDEF;
    return l;
}

/* The local declared as name in the innermost open scope, or -1: a second
 * declaration there is a redeclaration, while one in an inner scope only
 * hides it. */
int local_in_scope(int name)
{
    int l;

    l = local_of[name];
    return l >= 0 && locals[l].depth == scope_depth ? l : -1;
}

/* A function's outermost block, which its parameters share. */
void scope_function(void)
{
    scope_depth = 1;
    nblocks = 1;
    block_parent[0] = -1;
    cur_block = 0;
}

/* A compound statement's new scope and block. The marks record where the
 * locals and tags tables stood, so scope_leave knows what to hide. */
void scope_enter(void)
{
    if (scope_depth >= MAX_DEPTH)
        limit("nested blocks in one function");
    if (nblocks >= MAX_BLOCKS)
        limit("blocks in one function");
    local_mark[scope_depth] = nlocals;
    tag_mark[scope_depth] = ntags;
    block_mark[scope_depth] = cur_block;
    scope_depth++;
    block_parent[nblocks] = cur_block;
    cur_block = nblocks;
    nblocks++;
}

/* Leaving a block: its names and tags go out of scope (their entries stay,
 * for the frame layout and for the types that refer to them). */
void scope_leave(void)
{
    int i;

    scope_depth--;
    /* newest first: undoing the declarations in reverse order leaves each
     * name with the symbol it had on entry */
    for (i = nlocals - 1; i >= local_mark[scope_depth]; i--)
        if (locals[i].name >= 0)
            local_of[locals[i].name] = locals[i].shadowed;
    for (i = ntags - 1; i >= tag_mark[scope_depth]; i--) {
        if (tags[i].name >= 0)
            tag_of[tags[i].name] = tags[i].shadowed;
        tags[i].name = -1;
    }
    cur_block = block_mark[scope_depth];
}

/* Forget the locals added since mark (nlocals then): a parameter list's
 * names, which it made visible to its own later parameters. */
void drop_locals(int mark)
{
    while (nlocals > mark) {
        nlocals--;
        if (locals[nlocals].name >= 0)
            local_of[locals[nlocals].name] = locals[nlocals].shadowed;
    }
}

/* A nameless local for an expression's temporary (the result object of a
 * call returning a struct, double or long long, or an address expr.c
 * computes once and uses twice); func_end places temporaries after every
 * declared local, so they never move one (abi.md 5). */
int new_temp(int type)
{
    if (nlocals >= MAX_LOCALS)
        limit("locals in one function");
    locals[nlocals].name = -1;
    locals[nlocals].kind = LK_VAR;
    locals[nlocals].type = type;
    locals[nlocals].param = 0;
    locals[nlocals].offset = 0;
    locals[nlocals].temp = 1;
    locals[nlocals].shadowed = -1;
    locals[nlocals].depth = scope_depth;
    locals[nlocals].block = cur_block;
    nlocals++;
    return nlocals - 1;
}

/* An enumerator declared inside a function. */
int add_enum_local(int name, int val)
{
    int l;

    l = add_local(name, T_INT, 0);
    locals[l].kind = LK_ENUMC;
    locals[l].offset = val;
    return l;
}

/* -v: this file's tables, as peak/limit. */
void type_report(void)
{
    if (nlocals > peak_locals)
        peak_locals = nlocals;
    printf("cc1: types %d/%d, parameter types %d/%d, tags %d/%d, members %d/%d, locals %d/%d\n",
           ntypes, MAX_TYPES, nplist, MAX_PLIST, ntags, MAX_TAGS, nmembers, MAX_MEMBERS, peak_locals, MAX_LOCALS);
}

/* Forget the current function's locals and tags, at its end. The tags
 * declared inside it are the newest entries (local nonzero), so they are
 * unwound from the top of the table. */
void clear_locals(void)
{
    int i;

    if (nlocals > peak_locals)
        peak_locals = nlocals;
    for (i = 0; i < nlocals; i++)
        if (locals[i].name >= 0)
            local_of[locals[i].name] = -1;
    nlocals = 0;
    nparams_cur = 0;
    scope_depth = 0;
    nblocks = 0;
    cur_block = 0;
    for (i = ntags - 1; i >= 0 && tags[i].local; i--) {
        if (tags[i].name >= 0)
            tag_of[tags[i].name] = tags[i].shadowed;
        tags[i].local = 0;
        tags[i].name = -1;
    }
}
