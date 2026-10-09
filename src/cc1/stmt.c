/* stmt.c - declarations, statements, initialisers, and cc1's main.
 *
 *     cc1 in.i out.ir [-u unitname] [-w] [-Werror] [-ansi] [-v]
 *
 * The top of cc1's recursive descent parser. main reads the preprocessed
 * unit that cpp wrote, one external declaration at a time, and writes the
 * IR file that cc2 reads (docs/ir_format.md). expr.c parses, types and
 * folds the expressions inside, type.c keeps the types and symbols, lex.c
 * supplies the tokens, and emit.c writes the records.
 *
 * One pass over the tokens, streamed: a function's IR is written as its
 * statements are parsed, with its frame layout and switch tables after
 * its body (emit.c's func_end); file-scope objects are written as they
 * are defined, their initialisers item by item. Expression nodes live
 * only for their statement: a statement saves nnodes and resets it when
 * done (a `for` loop's third expression is kept until after its body, a
 * deferred initialiser's until it is stored).
 *
 * Control flow becomes IR labels and jumps (L, J, JF, JT), with label
 * numbers from new_label. reachable says whether control can reach the
 * current point, for the "control reaches the end" warning and main's
 * implicit return of 0; break_label, continue_label and cur_switch are
 * the innermost targets, saved and restored around each loop and switch.
 *
 * Sections: the current function; declaration specifiers (struct, union,
 * enum, the type keywords); declarators (pointers, arrays, functions,
 * parameter lists); static initialisers (IR data items); file-scope
 * declarations; statements (a switch on a long long, block-scope
 * declarations, inline assembly); function definitions (prototype and
 * K&R); main.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "args.h"
#include "io.h"
#include "int24.h"
#include "cc1.h"

/* both declared in cc1.h: -v, and the current function ends with '...' */
int verbose;
int func_variadic;

/* ---- the current function ------------------------------------------------------ */

static int cur_func;            /* global symbol of the function being compiled */
static int cur_ret;             /* its return type */
static int labels;              /* last IR label number used */
static int break_label;
static int continue_label;
static int reachable;           /* control can reach the current point */
static int has_break;           /* set by break inside the innermost loop */
static int close_line;          /* the line of the last block's closing brace */
static int cur_switch;          /* the innermost switch (emit.c's number), or -1 */

/* goto labels: function-scoped, by name. A goto may come before its
 * label, so a name gets its IR label at first sight, from a goto or the
 * label itself; function_def reports any never defined. */
static int glabel_name[MAX_LABELS];
static int glabel_num[MAX_LABELS];      /* the IR label */
static int glabel_line[MAX_LABELS];     /* where first used or defined */
static int glabel_defined[MAX_LABELS];
static int nglabels;
static int peak_glabels;

/* A fresh IR label number; the numbering restarts with each function. */
static int new_label(void)
{
    if (labels + 1 >= MAX_IR_LABELS)
        fatal("too many branches in one function (a cc1 table limit)");
    labels++;
    return labels;
}

/* The goto table's entry for name, made at its first use. */
static int goto_label(int name)
{
    int i;

    for (i = 0; i < nglabels; i++)
        if (glabel_name[i] == name)
            return i;
    if (nglabels >= MAX_LABELS)
        fatal("too many labels in one function (a cc1 table limit)");
    glabel_name[nglabels] = name;
    glabel_num[nglabels] = new_label();
    glabel_line[nglabels] = tok_line;
    glabel_defined[nglabels] = 0;
    nglabels++;
    return nglabels - 1;
}

/* ---- declaration specifiers ------------------------------------------------------ */

static int is_type_kw(int k)
{
    return k == KW_VOID || k == KW_CHAR || k == KW_INT || k == KW_UNSIGNED || k == KW_SIGNED
        || k == KW_SHORT || k == KW_LONG || k == KW_FLOAT || k == KW_DOUBLE || k == KW_CONST
        || k == KW_VOLATILE || k == KW_STRUCT || k == KW_UNION || k == KW_ENUM || k == KW_BOOL;
}

/* Whether the current token starts a declaration: a type keyword, a
 * storage class or a typedef name, except a typedef name followed by ':',
 * which is a statement label (labels have a name space of their own). */
int is_type_start(void)
{
    if (tok == TK_IDENT)
        return typedef_type(tok_name) >= 0 && peek() != TK_P + ':';
    return is_type_kw(tok) || tok == KW_STATIC || tok == KW_EXTERN || tok == KW_TYPEDEF
        || tok == KW_AUTO || tok == KW_REGISTER || tok == KW_INLINE;
}

/* At a '(': whether a type name follows, which makes it a cast or a
 * sizeof (type) rather than a parenthesised expression (expr.c). */
int is_type_start_after_paren(void)
{
    int k;

    k = peek();
    return is_type_kw(k) || (k == TK_IDENT && typedef_type(peek_name()) >= 0);
}

static int declared_tag;        /* the last decl_specs named or defined a tag */
static int decl_inline;         /* the last decl_specs had C99's inline */
static int in_params;           /* parsing a parameter list (register is allowed) */

/* "struct" or "union" [tag] ["{" members "}"], at the keyword; returns
 * the type. Without a member list the tag names the visible struct, or
 * declares an incomplete one; with one, it defines the tag in the current
 * scope (hiding any outer one), the members laid out by type.c's
 * add_member and add_field, and complete_struct fixes the size. */
static int struct_spec(void)
{
    int is_union;
    int w;
    char *kw;
    int name;
    int tg;
    int base;
    int sc;
    int mt;
    int mname;
    int n;

    is_union = tok == KW_UNION;
    kw = is_union ? "'union'" : "'struct'";
    next();
    name = -1;
    if (tok == TK_IDENT) {
        name = tok_name;
        next();
    }
    if (tok != TK_P + '{') {
        if (name < 0) {
            error_s(kw, " needs a tag or a member list");
            return T_INT;
        }
        tg = find_tag(name);
        if (tg >= 0 && tok == TK_P + ';' && tags[tg].local != scope_depth)
            tg = -1;        /* "struct tag;": a new tag, hiding an outer one (C89 3.5.2.3) */
        if (tg < 0) {
            tg = add_tag(name, 0, scope_depth);     /* incomplete until defined */
            tags[tg].is_union = is_union;
        }
        declared_tag = 1;
        if (tags[tg].is_enum || tags[tg].is_union != is_union) {
            error_s(is_union ? "'union' used with a struct or enum tag: " : "'struct' used with a union or enum tag: ",
                    name_str(name));
            return T_INT;
        }
        return tags[tg].type;
    }
    next();
    tg = name >= 0 ? find_tag(name) : -1;
    if (tg >= 0 && tags[tg].local != scope_depth)
        tg = -1;                                    /* a new tag hiding the outer one */
    if (tg >= 0 && (tags[tg].is_enum || tags[tg].complete || tags[tg].is_union != is_union)) {
        error_s("redefinition of tag ", name_str(name));
        tg = -1;
    }
    if (tg < 0) {
        tg = add_tag(name, 0, scope_depth);
        tags[tg].is_union = is_union;
    }
    n = 0;
    while (tok != TK_P + '}' && tok != TK_EOF) {
        base = decl_specs(&sc);
        if (sc != SC_NONE)
            error("storage class on a struct member");
        for (;;) {
            mname = -1;
            mt = tok == TK_P + ':' ? base : declarator(base, &mname);
            if (tags[tg].flex)
                error("a flexible array member must be the last member");
            if (accept(TK_P + ':')) {
                /* a bit-field: int, signed or unsigned int, _Bool (1 bit at
                 * most), or (an extension) char, short or long; at most 24 bits */
                w = const_expr();
                if (!is_integer(mt))
                    error("a bit-field must have an integer type");
                else if (w < 0 || w > 8 * type_size(mt) || w > 24 || (types[mt].kind == TY_BOOL && w > 1))
                    error("a bit-field wider than its type (or than 24 bits) or of negative width");
                else if (w == 0 && mname >= 0)
                    error("a named bit-field of width 0");
                else if (mname >= 0 && find_member(tg, mname) >= 0)
                    error_s("duplicate member ", name_str(mname));
                else if (add_field(tg, mname, unqual(mt), w) >= 0)
                    n++;
            } else if (mname < 0)
                error("a struct member needs a name");
            else if (types[mt].kind == TY_FUNC)
                error("a struct member cannot be a function");
            else if (!strict && types[mt].kind == TY_ARRAY && types[mt].count < 0
                     && type_size(types[mt].base) > 0) {
                /* C99's flexible array member: "T m[];" last, after a
                 * member, taking no space (sizeof stops before it) */
                if (is_union)
                    error("a flexible array member in a union");
                else if (n == 0)
                    error("a flexible array member needs a member before it");
                else if (find_member(tg, mname) >= 0)
                    error_s("duplicate member ", name_str(mname));
                else {
                    add_member(tg, mname, mt);
                    n++;
                    tags[tg].flex = 1;
                }
            } else if (type_size(mt) == 0)
                error_s("member of void or incomplete type: ", name_str(mname));
            else if (is_struct(mt) && tags[types[mt].base].flex)
                error("a struct with a flexible array member cannot be a member");
            else if (find_member(tg, mname) >= 0)
                error_s("duplicate member ", name_str(mname));
            else if (n >= 127)
                error("more than 127 members in a struct");
            else {
                add_member(tg, mname, mt);
                n++;
            }
            if (!accept(TK_P + ','))
                break;
        }
        expect(TK_P + ';', "';' after a struct member");
    }
    if (n == 0)
        error("a struct needs at least one member");
    expect(TK_P + '}', "'}' to end the struct");
    complete_struct(tg);
    declared_tag = 1;
    return tags[tg].type;
}

/* An enumeration constant: a local in a function, else a global
 * SK_ENUMC symbol holding its value. */
static void declare_enumerator(int name, int val)
{
    int g;

    if (in_function) {
        if (local_in_scope(name) >= 0)
            error_s("redeclaration in the same scope: ", name_str(name));
        else
            add_enum_local(name, val);
        return;
    }
    g = find_global(name);
    if (g >= 0) {
        error_s("redeclared as a different kind of symbol: ", name_str(name));
        return;
    }
    g = add_global(name, SK_ENUMC, T_INT, SC_NONE);
    globals[g].val = val;
    globals[g].defined = 1;
}

/* "enum" [tag] ["{" enumerators "}"], at the "enum"; an enum type is int
 *
 * Each enumerator without '=' is one more than the one before, wrapped
 * to 24 bits, the width of int. */
static int enum_spec(void)
{
    int name;
    int tg;
    int en;
    int v;
    int n;

    next();
    name = -1;
    if (tok == TK_IDENT) {
        name = tok_name;
        next();
    }
    declared_tag = 1;
    if (tok != TK_P + '{') {
        if (name < 0) {
            error("'enum' needs a tag or an enumerator list");
            return T_INT;
        }
        tg = find_tag(name);
        if (tg < 0)
            error_s("undefined enum ", name_str(name));
        else if (!tags[tg].is_enum)
            error_s("'enum' used with a struct tag: ", name_str(name));
        return T_INT;
    }
    next();
    if (name >= 0) {
        tg = find_tag(name);
        if (tg >= 0 && tags[tg].local == scope_depth)
            error_s("redefinition of tag ", name_str(name));
        add_tag(name, 1, scope_depth);
    }
    v = 0;
    n = 0;
    for (;;) {
        if (tok != TK_IDENT)
            fatal("expected an enumerator name");
        en = tok_name;
        next();
        if (accept(TK_P + '='))
            v = const_expr();
        if (n >= 127)
            error("more than 127 enumerators in an enum");
        declare_enumerator(en, v);
        v = wrap24(v + 1);
        n++;
        if (!accept(TK_P + ','))
            break;
        if (tok == TK_P + '}') {
            if (strict)
                error("a comma after the last enumerator (a C99 feature)");
            break;
        }
    }
    expect(TK_P + '}', "'}' to end the enum");
    declared_tag = 1;
    return T_INT;
}

/* Resolve C89 3.5.2's specifier combinations: base is char, int, void,
 * or -1 (none), or a struct, enum or typedef's type (other set). */
static int spec_type(int base, int other, int sign, int nshort, int nlong)
{
    if (other) {
        if (sign || nshort || nlong)
            error("more than one type in a declaration");
        return base;
    }
    if (nlong > 2)
        error("'long long long' is too long");
    else if (nlong == 2 && strict)
        error("'long long' is not C89");
    if (nshort && nlong)
        error("both 'short' and 'long'");
    if (base == T_VOID) {
        if (sign || nshort || nlong)
            error("'void' with 'short', 'long', 'signed' or 'unsigned'");
        return T_VOID;
    }
    if (base == T_BOOL) {
        if (sign || nshort || nlong)
            error("'_Bool' with 'short', 'long', 'signed' or 'unsigned'");
        return T_BOOL;
    }
    if (base == T_FLOAT) {
        if (sign || nshort || nlong)
            error("'float' with 'short', 'long', 'signed' or 'unsigned'");
        return T_FLOAT;
    }
    if (base == T_DOUBLE) {
        if (sign || nshort || nlong > 1)
            error("'double' with 'short', 'long long', 'signed' or 'unsigned'");
        return nlong ? T_LDOUBLE : T_DOUBLE;
    }
    if (base == T_CHAR) {
        if (nshort || nlong)
            error("'char' with 'short' or 'long'");
        return sign == 2 ? T_UCHAR : sign == 1 ? T_SCHAR : T_CHAR;
    }
    if (nshort)
        return sign == 2 ? T_USHORT : T_SHORT;
    if (nlong > 1)
        return sign == 2 ? T_ULLONG : T_LLONG;
    if (nlong)
        return sign == 2 ? T_ULONG : T_LONG;
    if (base < 0 && !sign) {
        warning("type defaults to int (implicit int)");
        return T_INT;
    }
    return sign == 2 ? T_UINT : T_INT;
}

/* The declaration specifiers at tok, in any order (C89 allows "long
 * unsigned const int"): the storage class to *sclass, the type, with its
 * qualifiers, returned. They are counted as they come and resolved by
 * spec_type at the end. An identifier is a typedef name only while no
 * other type word has been seen; after one it is the declarator's name,
 * as in "unsigned T;" with T also a typedef. */
int decl_specs(int *sclass)
{
    int sign;           /* 0 none, 1 signed, 2 unsigned */
    int nshort;
    int nlong;
    int base;
    int other;
    int qual;
    int q;

    *sclass = SC_NONE;
    sign = 0;
    nshort = 0;
    nlong = 0;
    base = -1;
    other = 0;
    qual = 0;
    declared_tag = 0;
    decl_inline = 0;
    for (;;) {
        if (tok == KW_INLINE) {
            decl_inline = 1;            /* its linkage: inline_linkage */
            next();
        } else if (tok == KW_STATIC || tok == KW_EXTERN || tok == KW_TYPEDEF || tok == KW_AUTO
                   || tok == KW_REGISTER) {
            if (*sclass != SC_NONE)
                error("more than one storage class");
            if (tok == KW_AUTO || tok == KW_REGISTER) {
                /* auto is the default and register only a hint: both leave
                 * SC_NONE (SC_AUTO marks them for the checks below) */
                if (!in_function && !in_params)
                    error("'auto' or 'register' outside a function");
                *sclass = SC_AUTO;
            } else {
                *sclass = tok == KW_STATIC ? SC_STATIC : tok == KW_EXTERN ? SC_EXTERN : SC_TYPEDEF;
            }
            next();
        } else if (tok == KW_CONST || tok == KW_VOLATILE) {
            q = tok == KW_CONST ? Q_CONST : Q_VOLATILE;
            if (qual & q)
                warning(tok == KW_CONST ? "duplicate 'const'" : "duplicate 'volatile'");
            qual = qual | q;
            next();
        } else if (tok == KW_SIGNED || tok == KW_UNSIGNED) {
            if (sign)
                error("more than one 'signed' or 'unsigned'");
            sign = tok == KW_SIGNED ? 1 : 2;
            next();
        } else if (tok == KW_SHORT) {
            nshort++;
            next();
        } else if (tok == KW_LONG) {
            nlong++;
            next();
        } else if (tok == KW_INT || tok == KW_CHAR || tok == KW_VOID || tok == KW_BOOL) {
            if (base >= 0)
                error("more than one type in a declaration");
            base = tok == KW_INT ? T_INT : tok == KW_CHAR ? T_CHAR : tok == KW_BOOL ? T_BOOL : T_VOID;
            next();
        } else if (tok == KW_STRUCT || tok == KW_UNION || tok == KW_ENUM) {
            if (base >= 0)
                error("more than one type in a declaration");
            base = tok == KW_ENUM ? enum_spec() : struct_spec();
            other = 1;
        } else if (tok == TK_IDENT && base < 0 && !sign && !nshort && !nlong
                   && typedef_type(tok_name) >= 0) {
            base = typedef_type(tok_name);
            other = 1;
            next();
        } else if (tok == KW_FLOAT || tok == KW_DOUBLE) {
            if (base >= 0)
                error("more than one type in a declaration");
            base = tok == KW_FLOAT ? T_FLOAT : T_DOUBLE;
            next();
        } else {
            break;
        }
    }
    if (*sclass == SC_AUTO)
        *sclass = SC_NONE;
    return qualify(spec_type(base, other, sign, nshort, nlong), qual);
}

/* ---- declarators ------------------------------------------------------------------- */

/* C's declarators read inside out: "int *(*fp)(void)" makes fp a pointer
 * to a function returning a pointer to int. declarator applies the
 * leading '*'s to the base type; a parenthesised inner declarator is
 * parsed against a placeholder type, the hole (type.c's make_hole); the
 * suffixes ('[' and '(') after it apply to the base; and the result is
 * put into the hole's place in the inner type (subst). The parameter
 * names of the last list parsed are kept in pnames for function_def. */

#define MAX_PNAMES 32
static int pnames[MAX_PNAMES];       /* parameter names of the last parameter list */
static int npnames;
static int kr_list;                  /* the last parameter list was a K&R identifier list */
static int nparam_lists;             /* parameter lists parsed, to tell which one a name has */
static int kr_names[MAX_PNAMES];     /* a K&R definition's parameters, and their declared types */
static int kr_types[MAX_PNAMES];
static int nkr;
static int hole = -1;                /* the placeholder type, made at first need */

/* t with the hole replaced by repl, rebuilt from the outside in. Only a
 * pointer, array or function type can hold it, in its base: a function's
 * parameters were complete types when it was made. */
static int subst(int t, int repl)
{
    int k;
    int ps[MAX_PNAMES];
    int i;

    if (t == hole)
        return repl;
    k = types[t].kind;
    if (k == TY_PTR)                    /* with its own qualifiers: (* const p) */
        return qualify(ptr_to(subst(types[t].base, repl)), types[t].qual);
    if (k == TY_ARRAY)
        return array_of(subst(types[t].base, repl), types[t].count);
    if (k == TY_FUNC) {
        for (i = 0; i < types[t].nparams; i++)
            ps[i] = plist[types[t].params + i];
        return func_type(subst(types[t].base, repl), ps, types[t].nparams, types[t].variadic);
    }
    return t;
}

/* The parameter list after a declarator's '(' (already read), through
 * its ')'; returns the type of a function returning ret. Three forms:
 * "()", no prototype (nparams -1); an identifier list, the parameters of
 * a K&R definition (kr_list set, the names in pnames, no prototype); and
 * a prototype ("void" alone for none, "..." after a named parameter). An
 * array or function parameter becomes a pointer (C89 3.7.1).
 *
 * A parameter's own declarator may hold a parameter list (a function
 * pointer), which resets pnames, so the names are collected in pn and
 * copied there at the end. A parameter is in scope from the end of its
 * declarator (C89 3.1.2.1), so the later ones can use it, as in
 * f(unsigned j, char d[sizeof j]): each is a local until the list ends. */
static int param_list(int ret)
{
    int ps[MAX_PNAMES];
    int pn[MAX_PNAMES];
    int n;
    int i;
    int variadic;
    int sc;
    int t;
    int name;
    int mark;

    n = 0;
    variadic = 0;
    npnames = 0;
    kr_list = 0;
    nparam_lists++;
    if (tok == TK_P + ')') {
        n = -1;                         /* "()": no prototype (C89 3.5.4.3) */
    } else if (tok == TK_IDENT && typedef_type(tok_name) < 0) {
        /* an identifier list: the parameters of a K&R definition, typed by
         * the declarations before its body; no prototype */
        kr_list = 1;
        for (;;) {
            if (tok != TK_IDENT) {
                error("expected a parameter name");
                break;
            }
            if (n >= MAX_ARGS_CALL)
                fatal("more than 31 parameters");
            pnames[n] = tok_name;
            n++;
            next();
            if (!accept(TK_P + ','))
                break;
        }
        npnames = n;
        expect(TK_P + ')', "')' after parameters");
        return func_type(ret, ps, -1, 0);
    } else if (tok == KW_VOID && peek() == TK_P + ')') {
        next();
    } else {
        mark = nlocals;
        for (;;) {
            if (accept(P_ELLIPSIS)) {
                if (n == 0)
                    error("'...' needs at least one named parameter before it");
                variadic = 1;
                break;
            }
            if (n >= MAX_ARGS_CALL)
                fatal("more than 31 parameters");
            in_params++;
            t = decl_specs(&sc);
            in_params--;
            if (sc != SC_NONE)
                error("storage class on a parameter");
            t = declarator(t, &name);
            if (types[t].kind == TY_ARRAY)
                t = ptr_to(types[t].base);
            if (types[t].kind == TY_FUNC)
                t = ptr_to(t);          /* a function parameter is a pointer to it (C89 3.7.1) */
            if (is_void(t))
                error("a parameter cannot be void");
            ps[n] = t;          /* qualified: the function's local keeps them */
            pn[n] = name;
            n++;
            if (name >= 0)
                add_local(name, t, 0);
            if (!accept(TK_P + ','))
                break;
        }
        drop_locals(mark);
    }
    for (i = 0; i < n; i++)
        pnames[i] = pn[i];
    npnames = n < 0 ? 0 : n;
    kr_list = 0;
    expect(TK_P + ')', "')' after parameters");
    return func_type(ret, ps, n, variadic);
}

/* The array and function suffixes after a declarator's name, applied to
 * base: "[n]" an array (n -1 when omitted), "(...)" a function. */
static int suffixes(int base)
{
    int n;
    int t;

    if (accept(TK_P + '[')) {
        n = -1;
        if (tok != TK_P + ']') {
            n = const_expr();
            if (n <= 0)
                error("array size must be positive");
        }
        expect(TK_P + ']', "']'");
        /* a[2][3] is an array of 2 arrays of 3: the later brackets bind first */
        t = suffixes(base);
        if (types[t].kind == TY_FUNC)
            error("array of functions");
        if (types[t].kind == TY_VOID)
            error("array of void");
        if (is_struct(t) && type_size(t) == 0)
            error("array of an incomplete struct");
        if (is_struct(t) && tags[types[t].base].flex)
            error("array of a struct with a flexible array member");
        if (types[t].kind == TY_ARRAY && types[t].count < 0)
            error("only an array's first dimension may be omitted");
        return array_of(t, n);
    }
    if (accept(TK_P + '(')) {
        t = param_list(base);
        if (tok == TK_P + '(' || tok == TK_P + '[')
            error("a function cannot return a function or an array");
        if (types[base].kind == TY_ARRAY || types[base].kind == TY_FUNC)
            error("a function cannot return a function or an array");
        return t;
    }
    return base;
}

/* A declarator applied to base: returns the declared type and sets *name
 * (-1 for an abstract declarator, as in a cast). pnames must end up with
 * the parameter names of the declared function itself, for a definition:
 * in "int (*f(int a))(char b)" f's own list (int a) is inside the
 * parentheses, and (char b) belongs to the function type that f returns
 * a pointer to, so the inner list's names are saved and put back after
 * the outer suffixes. */
int declarator(int base, int *name)
{
    int inner;
    int t;
    int before;
    int keep;
    int i;
    int saved[MAX_PNAMES];
    int nsaved;
    int saved_kr;

    while (accept(TK_P + '*')) {
        base = ptr_to(base);
        while (tok == KW_CONST || tok == KW_VOLATILE) {
            base = qualify(base, tok == KW_CONST ? Q_CONST : Q_VOLATILE);
            next();
        }
    }
    *name = -1;
    inner = -1;
    if (tok == TK_P + '(' && (peek() == TK_P + '*' || peek() == TK_P + '(' || peek() == TK_IDENT)) {
        next();
        if (hole < 0)
            hole = make_hole();
        before = nparam_lists;
        inner = declarator(hole, name);
        keep = nparam_lists != before;  /* the name's own parameter list was inside */
        nsaved = npnames;
        saved_kr = kr_list;
        for (i = 0; i < npnames; i++)
            saved[i] = pnames[i];
        expect(TK_P + ')', "')'");
    } else if (tok == TK_IDENT) {
        *name = tok_name;
        next();
    }
    t = suffixes(base);
    if (inner >= 0 && keep) {
        npnames = nsaved;               /* not the returned function type's list */
        kr_list = saved_kr;
        for (i = 0; i < nsaved; i++)
            pnames[i] = saved[i];
    }
    if (inner >= 0) {
        t = subst(inner, t);
    }
    return t;
}

/* A type name (C89 3.5.5): specifiers and an abstract declarator, for a
 * cast or sizeof. */
int parse_type_name(void)
{
    int sc;
    int t;
    int name;

    t = decl_specs(&sc);
    if (sc != SC_NONE)
        error("storage class in a type name");
    t = declarator(t, &name);
    if (name >= 0)
        error("a type name cannot declare a name");
    return t;
}

/* ---- initialisers ------------------------------------------------------------------- */

/* An object's initialiser (C89 3.5.7, C99 6.7.8) is gathered as records,
 * each some bytes at an offset in the object, held in order of offset,
 * and written as data items (ir_format.md 3) when it is complete, the gaps
 * between them and the rest of the object as Z. Holding them is what
 * C99's designators need: `{ [5] = 1, [2] = 3 }` and `{ .y = 1, .x = 2 }`
 * go back, and a later initialiser of the same subobject replaces the
 * earlier one. A record is one of
 *   'B' 'W' 'T'  an integer of 1, 2 or 3 bytes, a its value;
 *   'A'          an address constant, a the symbol and b the offset;
 *   'S'          len bytes of a string, at rec_pool + a;
 *   'X'          any other item (a Q or an H), its text at rec_pool + a;
 *   'D'          in a local aggregate's template or a compound literal in
 *                a function, an element that is not a constant: zeros in
 *                the template, and the store after the copy, of type a
 *                and expression b (whose nodes are kept until then).
 * A record placed over others replaces what it covers: a string's bytes
 * are trimmed to what is left; anything else (a scalar, or a union's
 * other member) goes whole.
 *
 * The records are a stack of frames, one per object being initialised:
 * an object's, and above it, while one of its initialisers holds a
 * compound literal, the literal's (compound_literal). A frame whose D is
 * already open (a defined object's, or a local aggregate's template) is
 * the bottom one; when the table or the pool fills, it writes out the
 * records that lie before the one being placed, and nothing may then go
 * before them (rec_flushed): designators that far back are refused, a
 * limit on an initialiser of more than MAX_RECS items. */
#define MAX_RECS 512
#define REC_POOL 4096

struct rec {
    int off;
    int len;
    int a;
    int b;
    char kind;
};

static struct rec recs[MAX_RECS];
static int nrecs;
static char rec_pool[REC_POOL];
static int pool_used;
static int frame_base;               /* the current frame's first record */
static int frame_open;               /* its D is open, so it may write early */
static int rec_flushed;              /* the bottom frame: the bytes written so far */
static int local_template;           /* 'D' records allowed (a function's template, in the default mode) */
static int keep_nodes;               /* static_item kept its expression: init_any keeps its nodes */
static int pending_init = -1;        /* braces elided: an expression already read for the first scalar */

/* ---- writing the records ---- */

/* Compound literals completed while an object's D is open: their D
 * records as text, written after its E (rec_kept_flush), as wide strings
 * are (emit.c). */
#define KEEP 4096
static char kept[KEEP];
static int kept_used;
static int keeping;                  /* rec_line goes to kept */

static void rec_line(char *s)
{
    int n;

    if (!keeping) {
        write_ir(s);
        return;
    }
    n = strlen(s);
    if (kept_used + n + 2 > KEEP)
        fatal("compound literals too large in one initialiser (a cc1 table limit)");
    strcpy(kept + kept_used, s);
    kept_used = kept_used + n;
    kept[kept_used] = '\n';
    kept_used++;
    kept[kept_used] = 0;
}

/* The compound literals kept, after the E of the object whose D was open. */
static void rec_kept_flush(void)
{
    if (kept_used > 0)
        out_str(ir_out, kept);
    kept_used = 0;
    kept[0] = 0;
}

/* n zero bytes, if n > 0 */
static void rec_zeros(int n)
{
    char buf[24];

    if (n > 0) {
        sprintf(buf, "Z %d", n);
        rec_line(buf);
    }
}

/* Record i as its data item. */
static void rec_item(int i)
{
    char buf[120];
    char *s;
    int v;
    int len;

    v = recs[i].a;
    switch (recs[i].kind) {
    case 'B':
        sprintf(buf, "B %d", ((v & 255) ^ 128) - 128);
        break;
    case 'W':
        sprintf(buf, "W %d", ((v & 65535) ^ 32768) - 32768);
        break;
    case 'T':
        sprintf(buf, "T %d", v);
        break;
    case 'A':
        sprintf(buf, "A %s %d", ir_sym(v), wrap24(recs[i].b));
        break;
    case 'X':
        rec_line(rec_pool + v);
        return;
    case 'D':
        sprintf(buf, "Z %d", recs[i].len);
        break;
    default:                            /* 'S': the text can be long, so in pieces */
        s = ir_string(rec_pool + v, recs[i].len);
        len = strlen(s);
        if (!keeping) {
            out_str(ir_out, "S ");
            out_str(ir_out, s);
            out_str(ir_out, "\n");
            return;
        }
        if (kept_used + len + 4 > KEEP)
            fatal("compound literals too large in one initialiser (a cc1 table limit)");
        strcpy(kept + kept_used, "S ");
        strcpy(kept + kept_used + 2, s);
        kept_used = kept_used + len + 2;
        kept[kept_used] = '\n';
        kept_used++;
        kept[kept_used] = 0;
        return;
    }
    rec_line(buf);
}

/* The current frame's records from first to last (not included), from
 * the object's byte at, as data items with the gaps zero-filled; returns
 * where they end. */
static int rec_write(int first, int last, int at)
{
    int i;

    for (i = first; i < last; i++) {
        rec_zeros(recs[i].off - at);
        rec_item(i);
        at = recs[i].off + recs[i].len;
    }
    return at;
}

/* ---- placing records ---- */

/* Moves the pool's live bytes (the 'S' and 'X' records') down, freeing
 * what records written or replaced held. Each is moved in order of its
 * place in the pool, so a move never overwrites one not yet moved. */
static void pool_compact(void)
{
    int done;
    int i;
    int best;
    int len;
    int last;

    done = 0;
    last = -1;
    for (;;) {
        best = -1;
        for (i = 0; i < nrecs; i++)
            if ((recs[i].kind == 'S' || recs[i].kind == 'X') && recs[i].a > last
                && (best < 0 || recs[i].a < recs[best].a))
                best = i;
        if (best < 0)
            break;
        last = recs[best].a;
        len = recs[best].kind == 'S' ? recs[best].len : (int)strlen(rec_pool + recs[best].a) + 1;
        memmove(rec_pool + done, rec_pool + recs[best].a, len);
        recs[best].a = done;
        done = done + len;
    }
    pool_used = done;
}

/* The table or the pool is full, as a record is to be placed at off: the
 * bottom frame writes out the records that end at or before off (a
 * positional initialiser goes on from off; a designator may still come
 * back to the gap after them). */
static void rec_full(int off)
{
    int k;

    if (frame_base != 0 || !frame_open)
        fatal("an initialiser too large for agonc to hold (more than 512 items, or 4096 bytes of strings)");
    k = 0;
    while (k < nrecs && recs[k].off + recs[k].len <= off) {
        if (recs[k].kind == 'D')
            fatal("a local aggregate's initialiser too large for agonc to hold with elements that are "
                  "not constants (more than 512 items)");
        k++;
    }
    if (k == 0)
        fatal("an initialiser too large for agonc to hold (more than 512 items, or 4096 bytes of strings)");
    rec_flushed = rec_write(0, k, rec_flushed);
    memmove(recs, recs + k, (nrecs - k) * sizeof(struct rec));
    nrecs = nrecs - k;
    pool_compact();
}

/* Room for a record at off (and a split one) and n bytes of pool, made by
 * writing out early if it must: before any of the pool is taken, since
 * writing out moves what the pool holds. */
static void rec_room(int off, int n)
{
    if (nrecs >= MAX_RECS - 1 || pool_used + n > REC_POOL)
        rec_full(off);
    if (nrecs >= MAX_RECS - 1 || pool_used + n > REC_POOL)
        fatal("an initialiser too large for agonc to hold (more than 512 items, or 4096 bytes of strings)");
}

/* n bytes of pool, which rec_room has made room for; returns where. */
static int pool_take(int n)
{
    int p;

    p = pool_used;
    pool_used = pool_used + n;
    return p;
}

/* Opens a gap at index i of the table. */
static void rec_insert(int i)
{
    memmove(recs + i + 1, recs + i, (nrecs - i) * sizeof(struct rec));
    nrecs++;
}

/* A record of len bytes at off in the current frame, replacing what it
 * covers; returns its index, or -1 if it cannot be placed. */
static int rec_put(int off, int len, int kind, int a, int b)
{
    int i;
    int end;
    int tail;

    rec_room(off, 0);
    if (frame_base == 0 && frame_open && off < rec_flushed) {
        error("a designator goes back before what agonc has already written of this initialiser "
              "(it holds 512 items)");
        return -1;
    }
    end = off + len;
    /* i: the first record that ends after off; those before it stay */
    i = nrecs;
    while (i > frame_base && recs[i - 1].off + recs[i - 1].len > off)
        i--;
    tail = -1;
    while (i < nrecs && recs[i].off < end) {
        if (recs[i].kind == 'S' && recs[i].off < off) {
            /* a string's head stays, and its tail if it reaches past end
             * (then nothing else can overlap) */
            tail = recs[i].off + recs[i].len > end;
            if (tail) {
                rec_insert(i + 1);
                recs[i + 1] = recs[i];
                recs[i + 1].a = recs[i].a + (end - recs[i].off);
                recs[i + 1].len = recs[i].off + recs[i].len - end;
                recs[i + 1].off = end;
            }
            recs[i].len = off - recs[i].off;
            i++;
            if (tail)
                break;
            continue;
        }
        if (recs[i].kind == 'S' && recs[i].off + recs[i].len > end) {
            /* a string's tail stays */
            recs[i].a = recs[i].a + (end - recs[i].off);
            recs[i].len = recs[i].off + recs[i].len - end;
            recs[i].off = end;
            break;
        }
        memmove(recs + i, recs + i + 1, (nrecs - i - 1) * sizeof(struct rec));
        nrecs--;
    }
    rec_insert(i);
    recs[i].off = off;
    recs[i].len = len;
    recs[i].kind = kind;
    recs[i].a = a;
    recs[i].b = b;
    return i;
}

/* An item's text (a Q or an H) as an 'X' record. */
static void rec_text(int off, int len, char *text)
{
    int p;

    rec_room(off, strlen(text) + 1);
    p = pool_take(strlen(text) + 1);
    strcpy(rec_pool + p, text);
    rec_put(off, len, 'X', p, 0);
}

/* A string's len bytes as an 'S' record. */
static void rec_string(int off, char *bytes, int len)
{
    int p;

    if (len == 0)
        return;
    rec_room(off, len);
    p = pool_take(len);
    memcpy(rec_pool + p, bytes, len);
    rec_put(off, len, 'S', p, 0);
}

/* The bits mask of the byte at off become bits, the rest kept: a
 * bit-field's share of a byte (a 'B' record, made if the byte had none). */
static void rec_bits(int off, int mask, int bits)
{
    int i;

    i = nrecs;
    while (i > frame_base && recs[i - 1].off > off)
        i--;
    if (i == frame_base || recs[i - 1].kind != 'B' || recs[i - 1].off != off) {
        i = rec_put(off, 1, 'B', 0, 0);
        if (i < 0)
            return;
    } else {
        i--;
    }
    recs[i].a = (recs[i].a & ~mask) | (bits & mask);
}

/* ---- the items ---- */

/* Whether n is an address constant (C89 3.4): a static object's or
 * function's address plus a constant. is_lvalue says n stands for an object,
 * whose address is wanted, rather than for a pointer value; an array value
 * is its first element's address. Sets *g and *off. */
static int address_constant(int n, int is_lvalue, int *g, int *off)
{
    int op;
    int k;

    op = nodes[n].op;
    if (op == EN_GVAR) {
        if (!is_lvalue && types[nodes[n].type].kind != TY_ARRAY && types[nodes[n].type].kind != TY_FUNC)
            return 0;                   /* the value of a variable is not a constant */
        *g = nodes[n].val;
        *off = 0;
        return 1;
    }
    if (op == EN_ADDR)
        return address_constant(nodes[n].a, 1, g, off);
    if (op == EN_DEREF && (is_lvalue || types[nodes[n].type].kind == TY_ARRAY))
        return address_constant(nodes[n].a, 0, g, off);        /* &*p is p */
    if (op == EN_CAST && !is_lvalue && (is_pointer(nodes[n].type) || is_pointer(value_type_of(nodes[n].a))))
        return address_constant(nodes[n].a, 0, g, off);
    if (op == EN_BIN && !is_lvalue && (nodes[n].val == B_ADD || nodes[n].val == B_SUB)) {
        if (nodes[nodes[n].b].op == EN_NUM && address_constant(nodes[n].a, 0, g, off)) {
            k = nodes[nodes[n].b].val;
            *off = nodes[n].val == B_ADD ? *off + k : *off - k;
            return 1;
        }
        if (nodes[n].val == B_ADD && nodes[nodes[n].a].op == EN_NUM && address_constant(nodes[n].b, 0, g, off)) {
            *off = *off + nodes[nodes[n].a].val;
            return 1;
        }
    }
    return 0;
}

/* One scalar of type t at off from expression n: a constant, or an
 * address constant (&obj, an array or string, either plus or minus a
 * constant). The item holds the bytes: Q a float's bits or a long, H a
 * double's or long long's eight bytes, B, W or T an integer of 1, 2 or 3
 * bytes (its low bytes, written as a signed value), A an address. The
 * casts that convert leaves are looked through, and an integer constant's
 * narrowing is left to the item's own truncation. In a template a
 * non-constant is a 'D' record, stored by code after the copy. */
static void static_item(int n, int t, int off)
{
    char buf[120];
    int g;
    int k;
    int op;
    int whole;
    struct i32 v;

    t = unqual(t);
    n = convert(n, t, "initialiser");
    whole = n;
    for (;;) {
        op = nodes[n].op;
        if (op == EN_CAST) {
            if (nodes[nodes[n].a].op == EN_NUM && is_integer(t) && !is_long(t) && !is_llong(t)) {
                /* a folded narrowing: evaluate it */
                n = nodes[n].a;
                op = EN_NUM;
            } else {
                n = nodes[n].a;
                continue;
            }
        }
        break;
    }
    if (op == EN_NUM && types[t].kind == TY_FLOAT) {
        sprintf(buf, "Q %lu", fbits[n].lo);
        rec_text(off, 4, buf);
        return;
    }
    if (op == EN_NUM && is_mem8(t)) {
        /* a double's or long long's eight bytes in memory order: little-endian, low word first */
        sprintf(buf, "H %02lx%02lx%02lx%02lx%02lx%02lx%02lx%02lx", fbits[n].lo & 255, fbits[n].lo >> 8 & 255,
                fbits[n].lo >> 16 & 255, fbits[n].lo >> 24 & 255, fbits[n].hi & 255, fbits[n].hi >> 8 & 255,
                fbits[n].hi >> 16 & 255, fbits[n].hi >> 24 & 255);
        rec_text(off, 8, buf);
        return;
    }
    if (op == EN_NUM) {
        if (is_long(t)) {
            i32_join(&v, nodes[n].hi8, nodes[n].val);
            strcpy(buf, "Q ");
            i32_str(buf + 2, &v, !is_unsigned(t));
            rec_text(off, 4, buf);
        } else {
            k = type_size(t);
            rec_put(off, k, k == 1 ? 'B' : k == 2 ? 'W' : 'T', nodes[n].val, 0);
        }
        return;
    }
    if (is_pointer(t) && address_constant(n, 0, &g, &k)) {
        rec_put(off, 3, 'A', g, k);
        return;
    }
    if (local_template) {
        rec_put(off, type_size(t), 'D', t, whole);
        keep_nodes = 1;
        return;
    }
    error("initialiser is not a constant");
}

/* An array a narrow string can initialise: of a 1-byte integer type. */
static int is_char_array(int t)
{
    return types[t].kind == TY_ARRAY && type_size(types[t].base) == 1 && is_integer(types[t].base);
}

/* An array a wide string can initialise: of wchar_t (int), or unsigned int. */
static int is_wchar_array(int t)
{
    return types[t].kind == TY_ARRAY && type_size(types[t].base) == 3 && is_integer(types[t].base);
}

/* A string literal for a char or (wide) wchar_t array at off; returns the
 * array's size in bytes. An array of unknown size takes the string's
 * length plus the terminator; one exactly the string's length gets no
 * terminator (C89 3.5.7 allows it); a longer one is zero-filled. A wide
 * string's characters are T records, 3 bytes each as tok_str holds them. */
static int init_string(int t, int off)
{
    int n;
    int k;
    int i;
    int count;
    int w;

    w = tok_wide ? 3 : 1;
    count = types[t].count;
    n = tok_len;
    if (count < 0)
        count = n + 1;
    if (n > count)
        error("string initialiser longer than the array");
    k = n < count ? n : count;
    if (tok_wide) {
        for (i = 0; i < k; i++)
            rec_put(off + 3 * i, 3, 'T', wide_char_at(tok_str + 3 * i), 0);
    } else {
        rec_string(off, tok_str, k);
    }
    if (k < count)
        rec_put(off + w * k, w, w == 1 ? 'B' : 'T', 0, 0);       /* the terminator */
    next();
    return w * count;
}

/* After too many initialisers, skip to the '}' of the current braces. */
static void skip_to_brace(void)
{
    int depth;

    error("too many initialisers");
    depth = 0;
    while (tok != TK_EOF && (depth > 0 || tok != TK_P + '}')) {
        if (tok == TK_P + '{')
            depth++;
        else if (tok == TK_P + '}')
            depth--;
        next();
    }
}

/* Past the rest of one initialiser (after an error), to the ',' or '}'
 * that follows it at this level. */
static void skip_initialiser(void)
{
    int depth;

    depth = 0;
    while (tok != TK_EOF && (depth > 0 || (tok != TK_P + ',' && tok != TK_P + '}'))) {
        if (tok == TK_P + '{')
            depth++;
        else if (tok == TK_P + '}')
            depth--;
        next();
    }
}

static int init_any(int t, int off);

/* Member m's initialiser, a bit-field's (an integer constant), into its
 * bytes of the struct at base: masked to the width, shifted to the
 * field's bit and merged into each byte it covers, the other fields' bits
 * kept. */
static void bf_init(int m, int base)
{
    int n;
    int v;
    int mask;
    int mark;
    int off;

    mark = nnodes;
    if (pending_init >= 0) {
        n = pending_init;
        pending_init = -1;
    } else if (accept(TK_P + '{')) {
        n = assign_expr();
        accept(TK_P + ',');
        expect(TK_P + '}', "'}'");
    } else {
        n = assign_expr();
    }
    if (nodes[n].op != EN_NUM || !is_integer(nodes[n].type)) {
        error("initialiser is not a constant");
        v = 0;
    } else if (types[members[m].type].kind == TY_BOOL) {
        v = nodes[n].val != 0;
    } else {
        v = nodes[n].val & (members[m].width < 24 ? (1 << members[m].width) - 1 : 0xFFFFFF);
    }
    nnodes = mark;
    mask = members[m].width < 24 ? (1 << members[m].width) - 1 : 0xFFFFFF;
    /* the field and its mask a byte at a time from its first; & 0xFFFF
     * drops the sign bits that >> copies in when the shifted value has
     * the top bit of a 24-bit int set */
    v = v << members[m].bit;
    mask = mask << members[m].bit;
    for (off = base + members[m].offset; mask != 0; off++) {
        rec_bits(off, mask & 255, v & 255);
        v = (v >> 8) & 0xFFFF;
        mask = (mask >> 8) & 0xFFFF;
    }
}

/* Element i (an array's) or member m (a struct's) of aggregate t at off:
 * its own initialiser. */
static void init_element(int t, int off, int i, int m)
{
    if (types[t].kind == TY_ARRAY)
        init_any(types[t].base, off + i * type_size(types[t].base));
    else if (members[m].width > 0)
        bf_init(m, off);
    else
        init_any(members[m].type, off + members[m].offset);
}

/* Whether tok starts a designator (C99), which only braces may hold. */
static int at_designator(void)
{
    return !strict && (tok == TK_P + '.' || tok == TK_P + '[');
}

static int init_members(int t, int off, int braced, int start, int cont);

/* A designator at aggregate t, at off: tok is its '[' or '.'. *pos gets the
 * element or member it names; then either more designators, for that
 * subobject, which is then initialised on from there as with its braces
 * elided, or '=' and its initialiser. */
static void designation(int t, int off, int *pos)
{
    int k;
    int et;
    int eoff;
    int m;
    int sub;

    /* the wrong kind of designator is reported and read past, and the
     * first element or member stands in for it */
    m = -1;
    if (types[t].kind == TY_ARRAY) {
        k = 0;
        if (accept(TK_P + '.')) {
            error("a member designator for an array");
            if (tok == TK_IDENT)
                next();
        } else {
            next();
            k = const_expr();
            expect(TK_P + ']', "']' after an array designator");
            if (k < 0 || (types[t].count >= 0 && k >= types[t].count)) {
                error("an array designator beyond the array's bounds");
                k = 0;
            }
        }
        *pos = k;
        et = types[t].base;
        eoff = off + k * type_size(et);
    } else {
        if (accept(TK_P + '[')) {
            error("an array designator for a struct");
            const_expr();
            expect(TK_P + ']', "']' after an array designator");
        } else {
            next();
            if (tok != TK_IDENT) {
                error("a member name after '.'");
            } else {
                m = find_member(types[t].base, tok_name);
                if (m < 0)
                    error_s("no member named ", name_str(tok_name));
                next();
            }
        }
        if (m < 0)
            m = tags[types[t].base].members;
        *pos = m;
        et = members[m].type;
        eoff = off + members[m].offset;
    }
    if (tok == TK_P + '.' || tok == TK_P + '[') {
        if ((types[et].kind != TY_ARRAY && !is_struct(et)) || (m >= 0 && members[m].width > 0)) {
            error("a designator for a part of something that is not an array or a struct");
            skip_initialiser();
            return;
        }
        designation(et, eoff, &sub);
        /* on from the next subobject, but in a union there is none */
        init_members(et, eoff, 0, types[et].kind == TY_ARRAY ? sub + 1
                     : tags[types[et].base].is_union ? -1 : members[sub].next, 1);
        return;
    }
    expect(TK_P + '=', "'=' after a designator");
    if (m >= 0 && members[m].width > 0)
        bf_init(m, off);
    else
        init_any(et, eoff);
}

/* The elements or members of aggregate t at off, from element or member
 * start (-1: none): inside braces (braced) up to the '}', or, with the
 * braces elided, as many as t has. cont: an initialiser came before, so
 * the first one here follows a comma (going on after a designator). A
 * comma before a '}', or one that the braces' own level needs (before a
 * designator, or when t has no room for another element), is left to it
 * when the braces are elided. A union takes one member, the first unless
 * designators name others. Returns the number of elements an array's
 * initialiser reached (its size, when unknown). */
static int init_members(int t, int off, int braced, int start, int cont)
{
    int i;
    int m;
    int k;
    int count;
    int high;
    int first;
    int done;
    int is_array;
    int is_union;

    is_array = types[t].kind == TY_ARRAY;
    is_union = !is_array && tags[types[t].base].is_union;
    count = is_array ? types[t].count : -1;
    i = is_array ? start : 0;
    m = is_array ? -1 : start;
    high = 0;
    done = cont;                        /* a union is done after one */
    for (first = 1;; first = 0) {
        if (!first || cont) {
            if (tok != TK_P + ',')
                break;
            k = peek();
            if (k == TK_P + '}') {
                if (braced)
                    next();             /* a comma before the '}' */
                break;
            }
            if (!strict && (k == TK_P + '.' || k == TK_P + '[')) {
                if (!braced)
                    break;
            } else if (is_array ? count >= 0 && i >= count : m < 0 || (is_union && done)) {
                break;                  /* no room: braced, too many (init_any reports it) */
            }
            next();
        } else if (tok == TK_P + '}') {
            break;
        }
        if (at_designator()) {
            if (!braced)
                break;
            if (is_array) {
                designation(t, off, &i);
                i++;
            } else {
                designation(t, off, &m);
                m = members[m].next;
            }
        } else {
            if (is_array ? count >= 0 && i >= count : m < 0 || (is_union && done))
                break;
            if (!is_array && types[members[m].type].kind == TY_ARRAY && types[members[m].type].count < 0) {
                error("a flexible array member cannot be initialised");
                break;
            }
            init_element(t, off, i, m);
            if (is_array)
                i++;
            else
                m = members[m].next;
        }
        done = 1;
        if (is_array && i > high)
            high = i;
    }
    return high;
}

/* A struct or union member without braces (C89 3.5.7, C99 6.7.8): an
 * expression of a struct type is the whole member's value (in a template
 * in the default mode; elsewhere it is not a constant); otherwise it is
 * the first scalar's, pending for init_members. Returns 1 if it was the
 * member's value. */
static int struct_value_init(int t, int off)
{
    int n;
    int mark;

    mark = nnodes;
    if (pending_init < 0)
        pending_init = assign_expr();
    if (!is_struct(value_type_of(pending_init)))
        return 0;
    n = pending_init;
    pending_init = -1;
    static_item(n, t, off);
    if (!keep_nodes)
        nnodes = mark;
    keep_nodes = 0;
    return 1;
}

/* An object of type t's initialiser at off, as records; returns its size
 * (for an array of unknown size, what the initialiser gave it). */
static int init_any(int t, int off)
{
    int n;
    int mark;

    /* An aggregate: a string for a char or wchar_t array (braced or not);
     * a struct given a whole struct value; braces elided; then braced. A
     * scalar may be braced too ("int x = { 1 };"). */
    if (types[t].kind == TY_ARRAY || is_struct(t)) {
        if (pending_init < 0 && tok == TK_STR && (is_char_array(t) || is_wchar_array(t))) {
            if (tok_wide != is_wchar_array(t))
                error(tok_wide ? "a wide string for a char array" : "a narrow string for a wchar_t array");
            return init_string(t, off);
        }
        if (is_struct(t) && tok != TK_STR && (pending_init >= 0 || tok != TK_P + '{') && struct_value_init(t, off))
            return type_size(t);
        if (pending_init >= 0 || !accept(TK_P + '{')) {
            n = init_members(t, off, 0, types[t].kind == TY_ARRAY ? 0 : tags[types[t].base].members, 0);
        } else {
            if (tok == TK_STR && (is_char_array(t) || is_wchar_array(t))) {
                if (tok_wide != is_wchar_array(t))
                    error(tok_wide ? "a wide string for a char array" : "a narrow string for a wchar_t array");
                n = init_string(t, off) / type_size(types[t].base);
            } else {
                n = init_members(t, off, 1, types[t].kind == TY_ARRAY ? 0 : tags[types[t].base].members, 0);
            }
            accept(TK_P + ',');
            if (tok != TK_P + '}')
                skip_to_brace();
            expect(TK_P + '}', "'}'");
        }
        if (types[t].kind == TY_ARRAY)
            return (types[t].count >= 0 ? types[t].count : n) * type_size(types[t].base);
        return type_size(t);
    }
    mark = nnodes;
    if (pending_init >= 0) {
        n = pending_init;
        pending_init = -1;
        static_item(n, t, off);
    } else if (accept(TK_P + '{')) {
        if (tok == TK_STR && is_integer(t))
            error("a string cannot initialise one char");
        static_item(assign_expr(), t, off);
        accept(TK_P + ',');
        if (tok != TK_P + '}')
            skip_to_brace();
        expect(TK_P + '}', "'}'");
    } else {
        if (tok == TK_STR && is_integer(t))
            error("a string cannot initialise one char");
        static_item(assign_expr(), t, off);
    }
    if (!keep_nodes)
        nnodes = mark;
    keep_nodes = 0;
    return type_size(t);
}

/* The initialiser of an object of type *t, whose D is open, as its data
 * items; completes an array of unknown size. The object is the bottom
 * frame, which may write early when the table fills. */
static void static_init(int *t)
{
    int n;

    nrecs = 0;
    pool_used = 0;
    frame_base = 0;
    frame_open = 1;
    rec_flushed = 0;
    n = init_any(*t, 0);
    if (types[*t].kind == TY_ARRAY && types[*t].count < 0 && type_size(types[*t].base) > 0)
        *t = array_of(types[*t].base, n / type_size(types[*t].base));
    rec_zeros(type_size(*t) - rec_write(0, nrecs, rec_flushed));
}

/* *(et *)((unsigned char *)&lv + off) = expression n: a template's
 * deferred element, stored after the copy into lv. */
static int deferred_store(int lv, int off, int et, int n)
{
    int p;

    p = node(EN_ADDR, ptr_to(nodes[lv].type), lv, -1, 0);
    p = node(EN_CAST, ptr_to(T_UCHAR), p, -1, 0);
    if (off != 0)
        p = node(EN_BIN, ptr_to(T_UCHAR), p, num(off, T_INT), B_ADD);
    p = node(EN_DEREF, et, node(EN_CAST, ptr_to(et), p, -1, 0), -1, 0);
    return node(EN_ASSIGN, et, p, n, 0);
}

/* C99's compound literal, (t){ ... }, at its '{': an unnamed object of
 * type t (an array of unknown size completed by the initialiser), as an
 * lvalue. Its value is a static object of its own, __ini<id>_<n>, whose D
 * is written when the literal is complete, or, if an object's D is open,
 * after that object's E. At file scope that object is the literal. In a
 * function, the literal is a temporary local that each evaluation fills:
 * the object is its template, copied in, and the elements that are not
 * constants are stored after the copy, as for a local aggregate; the
 * expression is *(copy, the stores, &temporary). */
int compound_literal(int t)
{
    char buf[60];
    int g;
    int n;
    int i;
    int tmp;
    int lv;
    int e;
    int save_base;
    int save_open;
    int save_pool;
    int save_template;
    int save_keeping;
    int save_pending;

    if (types[t].kind == TY_FUNC || is_void(t) || (type_size(t) == 0 && types[t].kind != TY_ARRAY)) {
        error("a compound literal of a function, void or an incomplete type");
        t = T_INT;
    }
    save_base = frame_base;
    save_open = frame_open;
    save_pool = pool_used;
    save_template = local_template;
    save_keeping = keeping;
    save_pending = pending_init;
    frame_base = nrecs;
    frame_open = 0;
    local_template = in_function && !strict;
    pending_init = -1;
    n = init_any(t, 0);
    if (types[t].kind == TY_ARRAY && types[t].count < 0) {
        t = array_of(types[t].base, n / type_size(types[t].base));
        if (type_size(t) == 0)
            error("a compound literal of an empty array");
    }
    t = unqual(t);
    g = init_template(t);
    keeping = in_static_init;
    sprintf(buf, "D %s s", ir_sym(g));
    rec_line(buf);
    rec_zeros(type_size(t) - rec_write(frame_base, nrecs, 0));
    rec_line("E");
    keeping = save_keeping;
    e = -1;
    if (in_function) {
        tmp = new_temp(t);
        lv = node(EN_LVAR, t, -1, -1, tmp);
        e = node(EN_ASSIGN, t, lv, node(EN_GVAR, t, -1, -1, g), 0);
        for (i = frame_base; i < nrecs; i++)
            if (recs[i].kind == 'D')
                e = node(EN_COMMA, recs[i].a, e, deferred_store(lv, recs[i].off, recs[i].a, recs[i].b), 0);
        e = node(EN_COMMA, ptr_to(t), e, node(EN_ADDR, ptr_to(t), lv, -1, 0), 0);
        e = node(EN_DEREF, t, e, -1, 0);
    }
    nrecs = frame_base;
    pool_used = save_pool;
    frame_base = save_base;
    frame_open = save_open;
    local_template = save_template;
    pending_init = save_pending;
    if (e >= 0)
        return e;
    return node(EN_GVAR, t, -1, -1, g);
}

/* ---- file-scope declarations ------------------------------------------------------- */

/* Whether function types a and b are compatible (C89 3.5.4.3): the same
 * return type, and parameters that match, or no prototype on one side and
 * on the other parameters the default promotions leave unchanged. A
 * parameter's own qualifiers do not count: int f(const int) and
 * int f(int x) declare the same function.
 *
 * (Types are interned by type.c: identical types share one index, so
 * this is asked only of two different types.) */
int compatible_funcs(int a, int b)
{
    int i;
    int k;

    if (unqual(types[a].base) != unqual(types[b].base))
        return 0;
    if (types[a].nparams >= 0 && types[b].nparams >= 0) {
        /* both prototyped: the same parameters, unqualified */
        if (types[a].nparams != types[b].nparams || types[a].variadic != types[b].variadic)
            return 0;
        for (i = 0; i < types[a].nparams; i++)
            if (unqual(plist[types[a].params + i]) != unqual(plist[types[b].params + i]))
                return 0;
        return 1;
    }
    if (types[a].nparams < 0) {
        k = a;
        a = b;
        b = k;
    }
    if (types[a].nparams < 0)
        return 1;
    if (types[a].variadic)
        return 0;
    for (i = 0; i < types[a].nparams; i++) {
        k = plist[types[a].params + i];
        if (promote(unqual(k)) != unqual(k))
            return 0;
    }
    return 1;
}

/* The file-scope symbol for name with type t, sc its storage class: made,
 * or the earlier declaration checked against this one. A prototype
 * replaces an unprototyped type, a complete array type an incomplete
 * one. Also used for a block-scope extern or function declaration. A
 * declaration supersedes an implicit one made by a call. */
static int declare_global(int name, int t, int sc)
{
    int g;
    int kind;
    int old;

    kind = types[t].kind == TY_FUNC ? SK_FUNC : SK_VAR;
    g = find_global(name);
    if (g < 0)
        return add_global(name, kind, t, sc == SC_STATIC ? SC_STATIC : SC_NONE);
    old = globals[g].type;
    if (globals[g].kind != kind) {
        error_s("redeclared as a different kind of symbol: ", name_str(name));
    } else if (old != t && kind == SK_FUNC && compatible_funcs(old, t)) {
        if (types[old].nparams < 0)
            globals[g].type = t;        /* the prototype wins */
    } else if (old != t) {
        if (kind == SK_VAR && types[t].kind == TY_ARRAY && types[old].kind == TY_ARRAY
            && types[t].base == types[old].base && (types[t].count < 0 || types[old].count < 0)) {
            if (types[old].count < 0)
                globals[g].type = t;
        } else {
            error_s("conflicting types for ", name_str(name));
        }
    }
    if (sc == SC_STATIC && globals[g].sclass != SC_STATIC)
        error_s("static declaration follows a non-static one: ", name_str(name));
    globals[g].implicit = 0;
    return g;
}

/* The linkage of a function declared inline without static or extern
 * (C99 6.7.4): an inline definition, which this unit keeps to itself, as
 * static, so that a header's inline function can be in several units; but
 * an external definition if the unit has already declared the function
 * without inline (agonc never inlines, so the function's code is made
 * either way, and ld leaves out a copy nobody calls). */
static int inline_linkage(int name)
{
    int g;

    g = find_global(name);
    return g >= 0 && globals[g].kind == SK_FUNC && globals[g].sclass != SC_STATIC ? SC_NONE : SC_STATIC;
}

/* An identical repeated typedef is accepted (as headers may do); any
 * other redeclaration is an error. */
static void declare_typedef(int name, int t)
{
    int g;

    g = find_global(name);
    if (g >= 0) {
        if (globals[g].kind != SK_TYPEDEF || globals[g].type != t)
            error_s("redeclared as a different kind of symbol: ", name_str(name));
        return;
    }
    g = add_global(name, SK_TYPEDEF, t, SC_NONE);
    globals[g].defined = 1;
}

/* The definition of global object g at tok: with '=' a D record and its
 * initialiser's items (which complete an array of unknown size), else a
 * G record for a zero-filled object. vis is g for external linkage, s
 * for internal. For file-scope definitions and block-scope statics; in a
 * function the record goes between its statements (ir_format.md 4). */
static void define_var(int g)
{
    int t;
    char buf[120];
    char *vis;

    t = globals[g].type;
    vis = globals[g].sclass == SC_STATIC ? "s" : "g";
    if (globals[g].defined)
        error_s("redefinition of ", name_str(globals[g].name));
    globals[g].defined = 1;
    if (accept(TK_P + '=')) {
        sprintf(buf, "D %s %s", ir_sym(g), vis);
        write_ir(buf);
        in_static_init = 1;
        static_init(&t);
        in_static_init = 0;
        write_ir("E");
        wide_flush();                   /* its wide strings and compound literals, after it */
        rec_kept_flush();
        globals[g].type = t;
    } else {
        if (type_size(t) == 0)
            error_s("size unknown for ", name_str(globals[g].name));
        sprintf(buf, "G %s %d %s", ir_sym(g), type_size(t), vis);
        write_ir(buf);
    }
}

/* ---- statements -------------------------------------------------------------------- */

static void statement(void);
static void compound(int scope);
static int block_statics;       /* block-scope statics in this unit, for their names */

/* The IR's control records: J n jumps to label n, L n places it. */
static void jump(int label)
{
    emit_fmt("J %d", label);
}

static void label_here(int label)
{
    emit_fmt("L %d", label);
}


/* ---- a switch on a long long (C99) ----
 * The IR's switch tables are 32-bit, so a switch on a long long keeps its
 * value in a temporary and compares it at each case: the tests chain from
 * the switch's start, the code before a case jumps over its test, and the
 * last test's failure goes to the default or the end. */
static int q_tmp = -1;          /* the innermost such switch's temporary */
static int q_next;              /* and the label of its next test */

/* switch (c) with c a long long of type t; mark is where the nodes stood
 * before c. After c is stored, a jump goes to the first test (q_next);
 * the body follows, only a case label reaching it; then the failure of
 * the last test goes to the default or the end (switch_exit). The
 * enclosing switch's state is saved and restored, so these nest. */
static void q_switch(int c, int t, int mark)
{
    int k;
    int end;
    int os;
    int ob;
    int ohb;
    int otmp;
    int onext;
    int r;

    otmp = q_tmp;
    onext = q_next;
    q_tmp = new_temp(t);
    emit_rvalue(node(EN_ASSIGN, t, node(EN_LVAR, t, -1, -1, q_tmp), c, 0));
    emit_line("DROP");
    nnodes = mark;
    end = new_label();
    k = switch_new(end, t);
    q_next = new_label();
    jump(q_next);
    os = cur_switch;
    ob = break_label;
    ohb = has_break;
    cur_switch = k;
    break_label = end;
    has_break = 0;
    reachable = 0;                      /* only a case label reaches the body */
    statement();
    r = reachable || has_break || !switch_has_default(k);
    jump(end);
    label_here(q_next);                 /* no case matched */
    jump(switch_exit(k));
    cur_switch = os;
    break_label = ob;
    has_break = ohb;
    label_here(end);
    reachable = r;
    q_tmp = otmp;
    q_next = onext;
}

/* A case of a long long switch: the code before it jumps past its test
 * (skip); the previous test's failure lands at q_next, here, and this test
 * compares the temporary with the value, failing to a new q_next.
 * switch_case only checks for a duplicate value (label 0: no table is
 * written for a long long switch). */
static void q_case(int mark)
{
    struct i64 v;
    struct i32 w;
    int skip;
    int n;

    n = case_test64(q_tmp, switch_type(cur_switch), &v);
    expect(TK_P + ':', "':' after case");
    i32_set(&w, (int)(v.lo >> 16 & 0xFFFF), (int)(v.lo & 0xFFFF));
    if (!switch_case(cur_switch, &w, v.hi, 0))
        error("duplicate case value");
    skip = new_label();
    jump(skip);                         /* the code before the case goes past its test */
    label_here(q_next);
    q_next = new_label();
    emit_cond(n, 0, q_next);
    nnodes = mark;
    label_here(skip);
    reachable = 1;
    statement();
}

/* A block-scope declaration. A typedef is a local that names a type; an
 * extern or a function declaration is a name for the file-scope symbol,
 * visible in this block only; a static is a file-scope object of its own,
 * defined at once (its D or G among the function's records). An
 * automatic variable's initialiser becomes code: an assignment for a
 * scalar, or for an aggregate a copy from a static template, then a store
 * for each element that was not a constant. */
static void local_decl(void)
{
    int sc;
    int base;
    int t;
    int name;
    int l;
    int n;
    int mark;
    int g;
    int i;
    int braced;
    char buf[80];

    base = decl_specs(&sc);
    if (accept(TK_P + ';')) {
        if (!declared_tag)
            warning("a declaration that declares nothing");
        return;
    }
    for (;;) {
        t = declarator(base, &name);
        l = name >= 0 ? local_in_scope(name) : -1;
        if (name < 0) {
            error("declaration without a name");
        } else if (sc == SC_TYPEDEF) {
            if (l >= 0 && !(locals[l].kind == LK_TYPEDEF && locals[l].type == t))
                error_s("redeclaration in the same scope: ", name_str(name));
            else if (l < 0)
                add_local_typedef(name, t);
        } else if (sc == SC_EXTERN || types[t].kind == TY_FUNC) {
            if (sc == SC_STATIC)
                error("a function declared static at block scope");
            /* the name refers to the file-scope symbol, but only in this block */
            g = declare_global(name, t, SC_EXTERN);
            if (l >= 0 && !(locals[l].kind == LK_GLOBAL && locals[l].offset == g))
                error_s("redeclaration in the same scope: ", name_str(name));
            else if (l < 0)
                add_local_global(name, g);
            if (tok == TK_P + '=')
                error("a block-scope extern takes no initialiser");
        } else if (sc == SC_STATIC) {
            /* a static object of its own, __s<id>_name_N (abi.md 7) */
            if (l >= 0)
                error_s("redeclaration in the same scope: ", name_str(name));
            block_statics++;
            sprintf(buf, "%s_%d", name_str(name), block_statics);
            g = add_global(intern(buf), SK_VAR, t, SC_STATIC);
            globals[g].used = 1;
            add_local_global(name, g);
            define_var(g);
            /* its initialiser may have completed an array's size */
            locals[find_local(name)].type = globals[g].type;
        } else {
            if (l >= 0)
                error_s("redeclaration in the same scope: ", name_str(name));
            if (is_void(t))
                error("variable of type void");
            else if (type_size(t) == 0 && !(types[t].kind == TY_ARRAY && tok == TK_P + '='))
                error_s("size unknown for ", name_str(name));
            l = add_local(name, t, 0);
            if (accept(TK_P + '=')) {
                if (types[t].kind == TY_ARRAY || (is_struct(t) && tok == TK_P + '{')) {
                    /* an aggregate: a static template, copied in */
                    g = init_template(t);
                    sprintf(buf, "D %s s", ir_sym(g));
                    write_ir(buf);
                    mark = nnodes;
                    in_static_init = 1;
                    local_template = !strict;
                    static_init(&t);
                    local_template = 0;
                    in_static_init = 0;
                    write_ir("E");
                    wide_flush();
                    rec_kept_flush();
                    globals[g].type = t;
                    locals[l].type = t;
                    if (type_size(t) == 0)
                        error_s("size unknown for ", name_str(name));
                    /* the copy: destination, source, COPY n */
                    emit_fmt("LA @%d", l);
                    emit_fmt_s("A %s", ir_sym(g));
                    emit_fmt("COPY %d", type_size(t));
                    emit_line("DROP");
                    /* the elements that are not constants, after the copy */
                    for (i = 0; i < nrecs; i++) {
                        if (recs[i].kind == 'D') {
                            emit_rvalue(deferred_store(node(EN_LVAR, t, -1, -1, l), recs[i].off, recs[i].a,
                                                       recs[i].b));
                            emit_line("DROP");
                        }
                    }
                    nnodes = mark;
                } else {
                    mark = nnodes;
                    braced = accept(TK_P + '{');
                    n = assign_expr();
                    if (braced) {
                        accept(TK_P + ',');
                        expect(TK_P + '}', "'}' after a scalar's initialiser");
                    }
                    n = node(EN_ASSIGN, t, node(EN_LVAR, t, -1, -1, l), convert(n, t, "initialiser"), 0);
                    emit_rvalue(n);
                    emit_line("DROP");
                    nnodes = mark;
                }
            }
        }
        if (!accept(TK_P + ','))
            break;
    }
    expect(TK_P + ';', "';' after a declaration");
}

/* "(" expression ")" for if, while, do and switch: a scalar. */
static int condition(void)
{
    int n;

    expect(TK_P + '(', "'('");
    n = expression();
    expect(TK_P + ')', "')'");
    if (!is_scalar(decay(nodes[n].type)))
        error("a condition must be a number or pointer");
    return n;
}

/* A constant nonzero condition (while (1)): such a loop ends only by a
 * break, which matters for reachability after it. */
static int always_true(int n)
{
    return nodes[n].op == EN_NUM && (nodes[n].val != 0 || nodes[n].hi8 != 0);
}

/* A loop body with its own break/continue targets; returns whether it
 * breaks out of this loop (for reachability after the loop). */
static int loop_body(int brk, int cont)
{
    int ob;
    int oc;
    int ohb;
    int b;

    ob = break_label;
    oc = continue_label;
    ohb = has_break;
    break_label = brk;
    continue_label = cont;
    has_break = 0;
    reachable = 1;
    statement();
    b = has_break;
    break_label = ob;
    continue_label = oc;
    has_break = ohb;
    return b;
}

/* ---- inline assembly ---- */

/* A character of a label name; the first may not be a digit. */
static int label_char(int c, int first)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (!first && c >= '0' && c <= '9');
}

/* A line of inline assembly that defines a label other than an @ one:
 * the whole program is one file for the assembler, so such a label must be
 * unique in the program (abi.md 10); a warning says so. */
static void asm_label_check(char *p)
{
    char name[MAX_IDENT + 1];
    int k;

    while (*p == ' ' || *p == '\t')
        p++;
    if (!label_char(*p, 1))
        return;
    for (k = 0; k < MAX_IDENT && label_char(p[k], 0); k++)
        name[k] = p[k];
    if (p[k] != ':')
        return;
    name[k] = 0;
    warning_s("a global label in inline assembly, which must be unique in the program (abi.md 10): ", name);
}

/* tok_str's lines as an ASM record (a trailing newline ends the last).
 *
 * For an asm("...") string and a #asm block (lex.c's TK_ASMB token) alike:
 * "ASM n" and the n lines, which cc2 copies verbatim (abi.md 10). The
 * lines are cut in place in tok_str. */
static void emit_asm(void)
{
    char *p;
    char *e;
    int n;
    int i;

    if (tok_len == 0)
        return;
    n = 1;
    for (i = 0; i < tok_len; i++)
        if (tok_str[i] == '\n')
            n++;
    if (tok_str[tok_len - 1] == '\n')
        n--;
    emit_fmt("ASM %d", n);
    p = tok_str;
    for (i = 0; i < n; i++) {
        e = strchr(p, '\n');
        if (e != NULL)
            *e = 0;
        asm_label_check(p);
        emit_line(p);
        if (e != NULL)
            p = e + 1;
    }
}

/* asm ( string ) ; */
static void asm_statement(void)
{
    expect(TK_P + '(', "'(' after asm");
    if (tok != TK_STR)
        fatal("asm needs a string literal");
    emit_asm();
    next();
    expect(TK_P + ')', "')'");
    expect(TK_P + ';', "';'");
}

/* One statement (C89 3.6), its IR emitted as it is parsed. The control
 * statements take this shape (c a condition, JF/JT jump when it is
 * false/true):
 *     if      JF c l1; then; [J l2;] L l1; [else; L l2]
 *     while   L l1; JF c l2; body; J l1; L l2          continue l1, break l2
 *     do      L l1; body; L l2; JT c l1; L l3          continue l2, break l3
 *     for     init; L l1; JF c l3; body; L l2; incr; J l1; L l3
 *                                                      continue l2, break l3
 *     switch  c; SW @k; body; L end                    break end
 * A switch's table (SWT k) goes after the function's body, when all its
 * cases are known. A statement that ends with a jump or return leaves
 * reachable 0; a label or case makes the code after it reachable. */
static void statement(void)
{
    int mark;
    int n;
    int c;
    int l1;
    int l2;
    int l3;
    int r1;
    int incr;
    int k;
    int os;
    int ob;
    int ohb;
    int t;
    int scoped;
    struct i32 cv;

    mark = nnodes;
    switch (tok) {
    case TK_P + '{':
        compound(1);
        return;
    case TK_P + ';':
        next();
        return;
    case KW_IF:
        next();
        c = condition();
        /* the jump over the else only when the then part can fall
         * through; after the if, reachable if either branch is */
        l1 = new_label();
        emit_cond(c, 0, l1);
        nnodes = mark;
        statement();
        r1 = reachable;
        if (accept(KW_ELSE)) {
            l2 = new_label();
            if (reachable)
                jump(l2);
            label_here(l1);
            reachable = 1;
            statement();
            label_here(l2);
            reachable = reachable || r1;
        } else {
            label_here(l1);
            reachable = 1;
        }
        return;
    case KW_WHILE:
        next();
        l1 = new_label();
        l2 = new_label();
        label_here(l1);
        c = condition();
        r1 = always_true(c);
        emit_cond(c, 0, l2);
        nnodes = mark;
        l3 = loop_body(l2, l1);
        jump(l1);
        label_here(l2);
        reachable = !r1 || l3;
        return;
    case KW_DO:
        next();
        l1 = new_label();
        l2 = new_label();
        l3 = new_label();
        label_here(l1);
        incr = loop_body(l3, l2);       /* (reused: whether the body breaks) */
        label_here(l2);
        if (tok != KW_WHILE)
            fatal("expected 'while' after a do body");
        next();
        c = condition();
        r1 = always_true(c);
        emit_cond(c, 1, l1);
        nnodes = mark;
        expect(TK_P + ';', "';' after do-while");
        label_here(l3);
        reachable = !r1 || incr;
        return;
    case KW_FOR:
        next();
        expect(TK_P + '(', "'(' after for");
        /* C99's for (int i = 0; ...): the declaration has a scope of its
         * own, around the whole statement, body included */
        scoped = !strict && is_type_start();
        if (scoped) {
            scope_enter();
            local_decl();               /* its ';' too */
        } else {
            if (tok != TK_P + ';') {
                n = expression();
                emit_rvalue(n);
                emit_line("DROP");
                nnodes = mark;
            }
            expect(TK_P + ';', "';' in for");
        }
        l1 = new_label();
        l2 = new_label();
        l3 = new_label();
        label_here(l1);
        r1 = 1;
        if (tok != TK_P + ';') {
            c = expression();
            if (!is_scalar(decay(nodes[c].type)))
                error("a condition must be a number or pointer");
            r1 = always_true(c);
            emit_cond(c, 0, l3);
            nnodes = mark;
        }
        expect(TK_P + ';', "';' in for");
        incr = -1;
        if (tok != TK_P + ')')
            incr = expression();        /* kept until after the body */
        expect(TK_P + ')', "')' in for");
        n = loop_body(l3, l2);
        label_here(l2);
        if (incr >= 0) {
            emit_rvalue(incr);
            emit_line("DROP");
        }
        nnodes = mark;
        jump(l1);
        label_here(l3);
        reachable = !r1 || n;
        if (scoped)
            scope_leave();
        return;
    case KW_BREAK:
        next();
        if (break_label < 0)
            error("break outside a loop");
        else
            jump(break_label);
        has_break = 1;
        reachable = 0;
        expect(TK_P + ';', "';' after break");
        return;
    case KW_CONTINUE:
        next();
        if (continue_label < 0)
            error("continue outside a loop");
        else
            jump(continue_label);
        reachable = 0;
        expect(TK_P + ';', "';' after continue");
        return;
    case KW_RETURN:
        next();
        if (tok == TK_P + ';') {
            if (!is_void(cur_ret))
                warning("'return;' in a function that returns a value");
            if (strcmp(name_str(globals[cur_func].name), "main") == 0) {
                /* the status is undefined in C89; 0, as when main falls
                 * off its end, since MOS reports any other as an error */
                emit_line("C 0");
                emit_line("RET");
            } else {
                emit_line("RETV");
            }
        } else {
            n = expression();
            if (is_void(cur_ret)) {
                error("return with a value in a void function");
            } else if (is_struct(cur_ret) || is_mem8(cur_ret)) {
                n = convert(n, cur_ret, "return");
                emit_rvalue(n);                 /* the struct's, double's or long long's address */
                emit_fmt("RETB %d", type_size(cur_ret));
            } else {
                n = convert(n, cur_ret, "return");
                emit_rvalue(n);
                emit_line("RET");
            }
            nnodes = mark;
        }
        reachable = 0;
        expect(TK_P + ';', "';' after return");
        return;
    case KW_ASM:
        next();
        asm_statement();
        return;
    case TK_ASMB:
        emit_asm();
        next();
        return;
    case KW_SWITCH:
        next();
        c = condition();
        t = unqual(decay(nodes[c].type));
        if (!is_integer(t)) {
            error("a switch needs an integer expression");
            t = T_INT;
        } else if (is_llong(t)) {
            q_switch(c, t, mark);
            return;
        }
        t = promote(t);
        l1 = new_label();               /* the end */
        k = switch_new(l1, t);
        emit_rvalue(c);
        emit_fmt("SW @%d", k);          /* its table (SWT) follows the body */
        nnodes = mark;
        os = cur_switch;
        ob = break_label;
        ohb = has_break;
        cur_switch = k;
        break_label = l1;
        has_break = 0;
        reachable = 0;                  /* only a case label reaches the body */
        statement();
        r1 = reachable || has_break || !switch_has_default(k);
        cur_switch = os;
        break_label = ob;
        has_break = ohb;
        label_here(l1);
        reachable = r1;
        return;
    case KW_CASE:
        next();
        if (cur_switch >= 0 && is_llong(switch_type(cur_switch))) {
            q_case(mark);
            return;
        }
        const_expr32(&cv, &t);
        expect(TK_P + ':', "':' after case");
        if (cur_switch < 0) {
            error("case outside a switch");
        } else {
            l1 = new_label();
            if (!switch_case(cur_switch, &cv, 0, l1))
                error("duplicate case value");
            label_here(l1);
        }
        reachable = 1;
        statement();
        return;
    case KW_DEFAULT:
        next();
        expect(TK_P + ':', "':' after default");
        if (cur_switch < 0) {
            error("default outside a switch");
        } else {
            l1 = new_label();
            if (!switch_default(cur_switch, l1))
                error("more than one default in a switch");
            label_here(l1);
        }
        reachable = 1;
        statement();
        return;
    case KW_GOTO:
        next();
        if (tok != TK_IDENT)
            fatal("expected a label name after goto");
        jump(glabel_num[goto_label(tok_name)]);
        next();
        reachable = 0;
        expect(TK_P + ';', "';' after goto");
        return;
    }
    /* a goto label, then an expression statement */
    if (tok == TK_IDENT && peek() == TK_P + ':') {
        k = goto_label(tok_name);
        if (glabel_defined[k])
            error_s("duplicate label ", name_str(tok_name));
        glabel_defined[k] = 1;
        glabel_line[k] = tok_line;
        label_here(glabel_num[k]);
        next();
        next();
        reachable = 1;
        statement();
        return;
    }
    n = expression();
    emit_rvalue(n);
    emit_line("DROP");
    nnodes = mark;
    expect(TK_P + ';', "';' after an expression");
}

/* A compound statement; a new scope unless it is a function's body (scope
 * 0), which shares its scope with the parameters (C89 3.1.2.1). */
static void compound(int scope)
{
    int decls_ok;

    expect(TK_P + '{', "'{'");
    if (scope)
        scope_enter();
    decls_ok = 1;
    while (tok != TK_P + '}' && tok != TK_EOF) {
        if (is_type_start()) {
            if (!decls_ok && strict)    /* C99 allows it; C89 wants a diagnostic */
                warning("declaration after a statement (a C99 feature)");
            local_decl();
        } else {
            decls_ok = 0;
            statement();
        }
    }
    close_line = tok_line;
    expect(TK_P + '}', "'}'");
    if (scope)
        scope_leave();
}

/* A K&R definition's parameter declarations, between its ")" and "{"; an
 * undeclared parameter is an int (implicit int, with a warning).
 *
 * The names come from the identifier list (pnames); their types go to
 * kr_types, for function_def. */
static void kr_declarations(void)
{
    int i;
    int sc;
    int base;
    int t;
    int name;

    nkr = npnames;
    for (i = 0; i < nkr; i++) {
        kr_names[i] = pnames[i];
        kr_types[i] = -1;
    }
    while (tok != TK_P + '{' && tok != TK_EOF) {
        in_params++;
        base = decl_specs(&sc);
        in_params--;
        if (sc != SC_NONE)
            error("storage class on a parameter");
        for (;;) {
            t = declarator(base, &name);
            for (i = 0; i < nkr && kr_names[i] != name; i++)
                ;
            if (i == nkr) {
                error_s("a declaration of something that is not a parameter: ", name >= 0 ? name_str(name) : "");
            } else if (kr_types[i] >= 0) {
                error_s("a parameter declared twice: ", name_str(name));
            } else {
                if (types[t].kind == TY_ARRAY)
                    t = ptr_to(types[t].base);
                if (types[t].kind == TY_FUNC)
                    t = ptr_to(t);
                if (is_void(t))
                    error("a parameter cannot be void");
                kr_types[i] = t;
            }
            if (!accept(TK_P + ','))
                break;
        }
        expect(TK_P + ';', "';' after a parameter declaration");
    }
    for (i = 0; i < nkr; i++) {
        if (kr_types[i] < 0) {
            warning_s("parameter defaults to int (implicit int): ", name_str(kr_names[i]));
            kr_types[i] = T_INT;
        }
    }
    if (!strict)
        warning("an old-style (K&R) function definition; a prototype-style one is checked at every call");
}

static int kr_float_param[MAX_PNAMES];  /* a K&R float parameter's double slot */
static int kr_float_local[MAX_PNAMES];  /* and the float local its name is */
static int nkr_float;

/* The definition of function g, at its '{' (any K&R declarations read):
 * the parameters become locals at their ABI slots, the F record opens
 * (func_begin), the body is parsed and streamed, and func_end writes the
 * frame layout, the switch tables and the E. Control falling off the end
 * of main returns 0; off another function, no value (with a warning if
 * it should return one). */
static void function_def(int g)
{
    int t;
    int i;
    int l;
    int mark;
    char *name;
    char flags[8];

    t = globals[g].type;
    name = name_str(globals[g].name);
    if (globals[g].defined)
        error_s("redefinition of ", name);
    globals[g].defined = 1;
    clear_locals();
    scope_function();
    if (is_struct(types[t].base) || is_mem8(types[t].base)) {
        if (is_struct(types[t].base) && type_size(types[t].base) == 0)
            error_s("returning an incomplete struct from ", name);
        nparams_cur = 1;                /* the hidden result pointer, at ix+6 (abi.md 4) */
    }
    nkr_float = 0;
    for (i = 0; i < nkr; i++) {
        /* a K&R definition: each argument arrives with the default promotions,
         * in slots its declared type reads (abi.md 4); a float arrives as a
         * double, in a parameter of its own, and is converted on entry into
         * a float local of the declared name */
        if (is_struct(kr_types[i]) && type_size(kr_types[i]) == 0) {
            error("a parameter has an incomplete struct type");
        } else if (local_in_scope(kr_names[i]) >= 0) {
            error_s("duplicate parameter name ", name_str(kr_names[i]));
        } else if (types[kr_types[i]].kind == TY_FLOAT) {
            kr_float_param[nkr_float] = add_local(intern("(float parameter)"), T_DOUBLE, 1);
            kr_float_local[nkr_float] = add_local(kr_names[i], kr_types[i], 0);
            nkr_float++;
        } else {
            add_local(kr_names[i], kr_types[i], 1);
        }
    }
    /* a prototype definition's parameters, named in pnames */
    for (i = 0; nkr == 0 && i < types[t].nparams; i++) {
        if (is_struct(plist[types[t].params + i]) && type_size(plist[types[t].params + i]) == 0)
            error("a parameter has an incomplete struct type");
        if (pnames[i] < 0)
            error("a parameter name is missing in a function definition");
        else if (local_in_scope(pnames[i]) >= 0)
            error_s("duplicate parameter name ", name_str(pnames[i]));
        else
            add_local(pnames[i], plist[types[t].params + i], 1);
    }
    cur_func = g;
    cur_func_name = name_str(globals[g].name);  /* for __func__ (expr.c) */
    cur_ret = unqual(types[t].base);
    func_variadic = types[t].variadic;
    labels = 0;
    break_label = -1;
    continue_label = -1;
    has_break = 0;
    reachable = 1;
    cur_switch = -1;
    if (nglabels > peak_glabels)
        peak_glabels = nglabels;
    nglabels = 0;
    in_function = 1;
    fp_used = 0;
    ll_used = 0;
    /* F's flags (ir_format.md 3): the return kind, and v if variadic */
    flags[0] = 0;
    if (is_struct(cur_ret) || is_mem8(cur_ret))
        strcat(flags, "r");
    else if (is_long(cur_ret) || types[cur_ret].kind == TY_FLOAT)
        strcat(flags, "l");
    else if (is_void(cur_ret))
        strcat(flags, "n");
    if (types[t].variadic)
        strcat(flags, "v");
    if (flags[0] == 0)
        strcat(flags, "-");
    func_begin(ir_sym(g), globals[g].sclass == SC_STATIC, nparams_cur, flags);
    mark = nnodes;
    for (i = 0; i < nkr_float; i++) {
        l = kr_float_local[i];
        emit_rvalue(node(EN_ASSIGN, T_FLOAT, node(EN_LVAR, T_FLOAT, -1, -1, l),
                         convert(node(EN_LVAR, T_DOUBLE, -1, -1, kr_float_param[i]), T_FLOAT, "parameter"), 0));
        emit_line("DROP");
        nnodes = mark;
    }
    compound(0);
    for (i = 0; i < nglabels; i++) {
        if (!glabel_defined[i]) {
            tok_line = glabel_line[i];
            error_s("goto to an undefined label ", name_str(glabel_name[i]));
        }
    }
    if (reachable) {
        if (strcmp(name, "main") == 0) {
            emit_line("C 0");
            emit_line("RET");
        } else {
            if (!is_void(cur_ret)) {
                tok_line = close_line;
                warning("control reaches the end of a function that returns a value");
            }
            emit_line("RETV");
        }
    }
    func_end();
    nkr = 0;
    in_function = 0;
    clear_locals();
}

/* One external declaration (C89 3.7): a function definition, or a
 * declaration with any number of declarators. The first declarator, if
 * it is a function followed by '{', or by the parameter declarations of a
 * K&R identifier list, begins a definition. */
static void external_decl(void)
{
    int sc;
    int base;
    int t;
    int name;
    int g;
    int first;
    int kr;
    int inl;

    if (tok == TK_ASMB || tok == KW_ASM) {
        /* c89_spec.md 13 item 1: a function or data written in assembly
         * goes in a .s file of its own */
        error("assembly outside a function: put it in a .s file (driver.md 2)");
        if (tok == KW_ASM) {
            while (tok != TK_P + ';' && tok != TK_EOF)
                next();
        }
        next();
        return;
    }
    base = decl_specs(&sc);
    inl = decl_inline;                  /* a parameter list's decl_specs resets it */
    if (accept(TK_P + ';')) {
        if (!declared_tag)
            warning("a declaration that declares nothing");
        return;
    }
    first = 1;
    for (;;) {
        t = declarator(base, &name);
        if (name < 0) {
            error("declaration without a name");
        } else if (sc == SC_TYPEDEF) {
            declare_typedef(name, t);
        } else if (types[t].kind == TY_FUNC) {
            if (inl && sc == SC_NONE)
                sc = inline_linkage(name);
            kr = kr_list && npnames > 0;
            if (first && (tok == TK_P + '{' || (kr && is_type_start()))) {
                if (kr)
                    kr_declarations();
                g = declare_global(name, t, sc);
                function_def(g);
                return;
            }
            if (kr)
                error("a parameter list without types belongs only in a function definition");
            g = declare_global(name, t, sc);
        } else {
            if (is_void(t))
                error("variable of type void");
            g = declare_global(name, t, sc);
            if (tok == TK_P + '=') {
                define_var(g);          /* with extern too: a definition (C89 3.7.2) */
            } else if (sc != SC_EXTERN && !globals[g].defined) {
                globals[g].tentative = 1;       /* defined at the unit's end if nothing else defines it */
            }
        }
        first = 0;
        if (!accept(TK_P + ','))
            break;
    }
    expect(TK_P + ';', "';' after a declaration");
}

/* ---- main ------------------------------------------------------------------------------ */

/* The unit id (object_format.md 2): the low 16 bits of the FNV-1a hash
 * of the unit name. 0x9DC5 and 0x193 are the low 16 bits of FNV-1a's
 * 32-bit offset basis (0x811C9DC5) and prime (0x01000193); XOR and
 * multiplication carry only upward, so working in 16 bits gives the
 * same low 16 bits. The id names the unit's static symbols (__s<id>_name,
 * abi.md 7) and its string objects. */
static unsigned unit_hash(char *s)
{
    unsigned h;

    h = 0x9DC5;
    while (*s) {
        h = ((h ^ (unsigned char)*s) * 0x193) & 0xFFFF;
        s++;
    }
    return h;
}

/* The unit's end: every tentative definition still without an initialised
 * one becomes a zero-filled object (C89 3.7.2); an array still of unknown
 * size gets one element. */
static void tentative_definitions(void)
{
    int i;
    int t;
    char buf[120];

    for (i = 0; i < nglobals; i++) {
        if (globals[i].kind != SK_VAR || !globals[i].tentative || globals[i].defined)
            continue;
        tok_line = globals[i].line;
        t = globals[i].type;
        if (types[t].kind == TY_ARRAY && types[t].count < 0) {
            warning_s("an array assumed to have one element: ", name_str(globals[i].name));
            t = array_of(types[t].base, 1);
            globals[i].type = t;
        }
        globals[i].defined = 1;
        if (type_size(t) == 0) {
            error_s("size unknown for ", name_str(globals[i].name));
            continue;
        }
        sprintf(buf, "G %s %d %s", ir_sym(i), type_size(t), globals[i].sclass == SC_STATIC ? "s" : "g");
        write_ir(buf);
    }
}

static char *args[MAX_ARGS];            /* the arguments, response files expanded */

/* Options, the input (lex_open) and output files, the IR 2 and U
 * records, then external declarations to the end of the input and the
 * unit's end: tentative definitions and the checks of static functions.
 * Every failure returns 200, the driver's failure status; an output with
 * errors is removed. */
int main(int argc, char **argv)
{
    int n;
    int i;
    char *in_path;
    char *out_path;
    char *uname;
    char *p;
    char buf[120];

    n = expand_args("cc1", argc, argv, args);
    if (n < 0)
        return 200;
    in_path = NULL;
    out_path = NULL;
    uname = NULL;
    for (i = 0; i < n; i++) {
        if (strcmp(args[i], "-u") == 0 && i + 1 < n) {
            i++;
            uname = args[i];
        } else if (strcmp(args[i], "-w") == 0) {
            warnings_off = 1;
        } else if (strcmp(args[i], "-Werror") == 0) {
            werror = 1;
        } else if (strcmp(args[i], "-ansi") == 0) {
            strict = 1;
        } else if (strcmp(args[i], "-v") == 0) {
            verbose = 1;
        } else if (args[i][0] == '-') {
            fprintf(stderr, "cc1: unknown option %s\n", args[i]);
            return 200;
        } else if (in_path == NULL) {
            in_path = args[i];
        } else if (out_path == NULL) {
            out_path = args[i];
        } else {
            fprintf(stderr, "cc1: too many file names\n");
            return 200;
        }
    }
    if (in_path == NULL || out_path == NULL) {
        fprintf(stderr, "usage: cc1 in.i out.ir [-u unitname] [-ansi] [-w] [-Werror] [-v]\n");
        return 200;
    }
    if (uname == NULL) {
        /* the input's base name */
        uname = in_path;
        for (p = in_path; *p; p++)
            if (*p == '/' || *p == '\\' || *p == ':')
                uname = p + 1;
    }
    if (strlen(uname) > 60) {           /* unit_name holds 64 */
        fprintf(stderr, "cc1: unit name too long\n");
        return 200;
    }
    strcpy(unit_name, uname);
    sprintf(unit_hex, "%04x", unit_hash(unit_name));
    type_init();
    emit_init();
    if (!lex_open(in_path)) {
        fprintf(stderr, "cc1: cannot open %s\n", in_path);
        return 200;
    }
    ir_path = out_path;
    ir_out = fopen(out_path, "wb");
    if (ir_out == NULL) {
        fprintf(stderr, "cc1: cannot create %s\n", out_path);
        lex_close();                    /* MOS would not close the input */
        return 200;
    }
    write_ir("IR 2");
    sprintf(buf, "U %s %s", unit_name, unit_hex);
    write_ir(buf);
    next();
    while (tok != TK_EOF)
        external_decl();
    tentative_definitions();
    for (i = 0; i < nglobals; i++) {
        if (globals[i].kind == SK_FUNC && globals[i].sclass == SC_STATIC) {
            tok_line = globals[i].line;
            if (globals[i].used && !globals[i].defined)
                error_s("static function used but never defined: ", name_str(globals[i].name));
            else if (!globals[i].used && globals[i].defined && !warnings_off)
                warning("unused static function");
        }
    }
    lex_close();
    fclose(ir_out);
    ir_out = NULL;
    if (verbose) {
        printf("cc1: peaks: nodes %d/%d, globals %d/%d\n",
               peak_nodes, MAX_NODES, nglobals, MAX_GLOBALS);
        lex_report();
        type_report();
        emit_report();
        if (nglabels > peak_glabels)
            peak_glabels = nglabels;
        printf("cc1: goto labels %d/%d\n",
               peak_glabels, MAX_LABELS);
    }
    if (errors) {
        remove(out_path);
        return 200;
    }
    return 0;
}
