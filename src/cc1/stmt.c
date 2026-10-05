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
        || k == KW_VOLATILE || k == KW_STRUCT || k == KW_UNION || k == KW_ENUM;
}

/* Whether the current token starts a declaration: a type keyword, a
 * storage class or a typedef name, except a typedef name followed by ':',
 * which is a statement label (labels have a name space of their own). */
int is_type_start(void)
{
    if (tok == TK_IDENT)
        return typedef_type(tok_name) >= 0 && peek() != TK_P + ':';
    return is_type_kw(tok) || tok == KW_STATIC || tok == KW_EXTERN || tok == KW_TYPEDEF
        || tok == KW_AUTO || tok == KW_REGISTER;
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
            if (accept(TK_P + ':')) {
                /* a bit-field: int, signed or unsigned int, or (an extension)
                 * char, short or long; at most 24 bits */
                w = const_expr();
                if (!is_integer(mt))
                    error("a bit-field must have an integer type");
                else if (w < 0 || w > 8 * type_size(mt) || w > 24)
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
            else if (type_size(mt) == 0)
                error_s("member of void or incomplete type: ", name_str(mname));
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
    for (;;) {
        if (tok == KW_STATIC || tok == KW_EXTERN || tok == KW_TYPEDEF || tok == KW_AUTO
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
        } else if (tok == KW_INT || tok == KW_CHAR || tok == KW_VOID) {
            if (base >= 0)
                error("more than one type in a declaration");
            base = tok == KW_INT ? T_INT : tok == KW_CHAR ? T_CHAR : T_VOID;
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
            if (n >= MAX_PNAMES)
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

/* ---- static initialisers ------------------------------------------------------------ */

/* Items are written as they are parsed, inside the D that define_var has
 * already opened (IR 2 streams initialisers, ir_format.md 3).
 *
 * The parse follows C89 3.5.7: init_any takes one object; an aggregate's
 * elements or members go through init_members, inside braces or, with
 * the braces elided, as many initialisers as it has elements. Each
 * returns the bytes it wrote, so that the caller can pad with zero bytes
 * (Z) to the next member's offset and to the end of the object.
 *
 * A local aggregate's initialiser in the default mode may have elements
 * that are not constants (C99 allows them; C89 does not). Its template
 * holds zeros for them, and they are stored after the template is copied
 * in: each one's position in the object, type and expression (whose nodes
 * are kept until then). init_base is the position of the aggregate that
 * init_members is filling. */
#define MAX_DEFERRED 64
static int local_template;           /* the template of a local aggregate, in the default mode */
static int init_base;
static int ndeferred;
static int deferred_off[MAX_DEFERRED];
static int deferred_type[MAX_DEFERRED];
static int deferred_node[MAX_DEFERRED];
static int keep_nodes;               /* static_item deferred its expression: init_any keeps its nodes */
static int pending_init = -1;        /* braces elided: an expression already read for the first scalar */

/* One data item, a line of IR. */
static void init_item(char *s)
{
    write_ir(s);
}

/* An S item: the bytes of a string, no terminator. */
static void string_item(char *bytes, int len)
{
    out_str(ir_out, "S ");                 /* in pieces: the text can be long */
    out_str(ir_out, ir_string(bytes, len));
    out_str(ir_out, "\n");
}

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

/* One scalar item of type t from expression n: a constant, or an address
 * constant (&obj, an array or string, either plus or minus a constant).
 * The item holds the bytes: Q a float's bits or a long, H a double's or
 * long long's eight bytes, B, W or T an integer of 1, 2 or 3 bytes (its
 * low bytes, written as a signed value), A an address. The casts that
 * convert leaves are looked through, and an integer constant's narrowing
 * is left to the item's own truncation. In a local aggregate's template
 * a non-constant is deferred: Z bytes here, stored by code later. */
static void static_item(int n, int t)
{
    char buf[120];
    int off;
    int g;
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
        init_item(buf);
        return;
    }
    if (op == EN_NUM && is_mem8(t)) {
        /* a double's or long long's eight bytes in memory order: little-endian, low word first */
        sprintf(buf, "H %02lx%02lx%02lx%02lx%02lx%02lx%02lx%02lx", fbits[n].lo & 255, fbits[n].lo >> 8 & 255,
                fbits[n].lo >> 16 & 255, fbits[n].lo >> 24 & 255, fbits[n].hi & 255, fbits[n].hi >> 8 & 255,
                fbits[n].hi >> 16 & 255, fbits[n].hi >> 24 & 255);
        init_item(buf);
        return;
    }
    if (op == EN_NUM) {
        if (is_long(t)) {
            i32_join(&v, nodes[n].hi8, nodes[n].val);
            strcpy(buf, "Q ");
            i32_str(buf + 2, &v, !is_unsigned(t));
        } else if (type_size(t) == 1) {
            sprintf(buf, "B %d", ((nodes[n].val & 255) ^ 128) - 128);
        } else if (type_size(t) == 2) {
            sprintf(buf, "W %d", ((nodes[n].val & 65535) ^ 32768) - 32768);
        } else {
            sprintf(buf, "T %d", nodes[n].val);
        }
        init_item(buf);
        return;
    }
    if (is_pointer(t) && address_constant(n, 0, &g, &off)) {
        sprintf(buf, "A %s %d", ir_sym(g), wrap24(off));
        init_item(buf);
        return;
    }
    if (local_template && ndeferred < MAX_DEFERRED) {
        deferred_off[ndeferred] = init_base;
        deferred_type[ndeferred] = t;
        deferred_node[ndeferred] = whole;
        ndeferred++;
        keep_nodes = 1;
        sprintf(buf, "Z %d", type_size(t));
        init_item(buf);
        return;
    }
    error("initialiser is not a constant");
}

/* n zero bytes, if n > 0. */
static void zero_fill(int n)
{
    char buf[24];

    if (n > 0) {
        sprintf(buf, "Z %d", n);
        init_item(buf);
    }
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

/* A wide string literal for a wchar_t array; returns the bytes written.
 *
 * As init_string, in 3-byte T items (tok_str holds a wide string's
 * characters 3 bytes each). */
static int init_wstring(int t)
{
    char buf[16];
    int n;
    int k;
    int i;
    int count;

    count = types[t].count;
    n = tok_len;
    if (count < 0)
        count = n + 1;
    if (n > count)
        error("string initialiser longer than the array");
    k = n < count ? n : count;
    for (i = 0; i < k; i++) {
        sprintf(buf, "T %d", wide_char_at(tok_str + 3 * i));
        init_item(buf);
    }
    if (k < count) {
        init_item("T 0");
        zero_fill(3 * (count - k - 1));
    }
    next();
    return 3 * count;
}

/* A string literal for a char array; returns the bytes written.
 *
 * An array of unknown size takes the string's length plus the
 * terminator; one exactly the string's length gets no terminator (C89
 * 3.5.7 allows it); a longer one is zero-filled. */
static int init_string(int t)
{
    int n;
    int k;
    int count;

    count = types[t].count;
    n = tok_len;
    if (count < 0)
        count = n + 1;
    if (n > count)
        error("string initialiser longer than the array");
    k = n < count ? n : count;
    string_item(tok_str, k);
    if (k < count) {
        init_item("B 0");
        zero_fill(count - k - 1);
    }
    next();
    return count;
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

static int init_any(int t);

/* Bit-field initialisers are packed into bytes (a run of bit-fields shares
 * them) and written when the run ends. */
static int bf_start;            /* the pending bytes' offset in the struct */
static int bf_len;
static int bf_bytes[4];

/* Writes the pending bit-field bytes as B items; returns their count. */
static int bf_flush(void)
{
    char buf[16];
    int i;

    for (i = 0; i < bf_len; i++) {
        sprintf(buf, "B %d", ((bf_bytes[i] & 255) ^ 128) - 128);
        init_item(buf);
    }
    i = bf_len;
    bf_len = 0;
    return i;
}

/* Member m's initialiser (an integer constant) into the pending bytes;
 * done is the bytes already written. Returns the bytes written now.
 *
 * A field whose offset lies past the pending bytes ends the run (written
 * out); a new run starts at the field's offset, zero fill before it. The
 * value is masked to the width, shifted to the field's bit and ORed in a
 * byte at a time. */
static int bf_init(int m, int done)
{
    int n;
    int v;
    int k;
    int i;
    int mark;
    int wrote;

    wrote = 0;
    if (bf_len > 0 && members[m].offset >= bf_start + bf_len)
        wrote = bf_flush();
    if (bf_len == 0) {
        bf_start = members[m].offset;
        if (bf_start > done + wrote) {
            zero_fill(bf_start - done - wrote);
            wrote = bf_start - done;
        }
    }
    k = members[m].bit + members[m].width;          /* bits from the first byte's bit 0 */
    while (bf_len < members[m].offset - bf_start + (k + 7) / 8) {
        bf_bytes[bf_len] = 0;
        bf_len++;
    }
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
    } else {
        v = nodes[n].val & ((1 << members[m].width) - 1);
    }
    nnodes = mark;
    v = v << members[m].bit;
    /* & 0xFFFF drops the sign bits that >> copies in when the shifted
     * value has the top bit of a 24-bit int set */
    for (i = members[m].offset - bf_start; v != 0; i++) {
        bf_bytes[i] = bf_bytes[i] | (v & 255);
        v = (v >> 8) & 0xFFFF;
    }
    return wrote;
}

/* The elements or members of aggregate t: inside braces (braced) up to the
 * '}', or, with the braces elided, as many as t has (C89 3.5.7). A comma
 * before a '}' is left to the braces' own level. Returns the bytes
 * written, zero fill included. */
static int init_members(int t, int braced)
{
    int i;
    int m;
    int done;
    int count;
    int et;
    int base;

    /* i counts the elements; for a struct m walks the member list, and
     * zero fill pads to each member's offset; init_base moves to each
     * element's position, for a deferred store */
    done = 0;
    i = 0;
    m = types[t].kind == TY_ARRAY ? -1 : tags[types[t].base].members;
    count = types[t].kind == TY_ARRAY ? types[t].count : -1;
    for (;;) {
        if (types[t].kind == TY_ARRAY ? count >= 0 && i >= count : m < 0)
            break;
        if (i > 0 && types[t].kind != TY_ARRAY && tags[types[t].base].is_union)
            break;                      /* a union: its first member only */
        if (tok == TK_P + '}')
            break;
        if (i > 0) {
            if (tok != TK_P + ',' || (!braced && peek() == TK_P + '}'))
                break;
            next();
            if (tok == TK_P + '}')
                break;
        }
        if (types[t].kind != TY_ARRAY && members[m].width > 0) {
            done = done + bf_init(m, done);
        } else {
            if (bf_len > 0)
                done = done + bf_flush();
            if (types[t].kind != TY_ARRAY && members[m].offset > done) {
                zero_fill(members[m].offset - done);
                done = members[m].offset;
            }
            et = types[t].kind == TY_ARRAY ? types[t].base : members[m].type;
            base = init_base;
            init_base = base + done;
            done = done + init_any(et);
            init_base = base;
        }
        i++;
        if (m >= 0)
            m = members[m].next;
    }
    if (types[t].kind == TY_ARRAY) {
        if (count < 0)
            count = i;
        zero_fill(count * type_size(types[t].base) - done);
        return count * type_size(types[t].base);
    }
    if (bf_len > 0)
        done = done + bf_flush();
    zero_fill(type_size(t) - done);
    return type_size(t);
}

/* A struct or union member without braces (C89 3.5.7, C99 6.7.8): an
 * expression of a struct type is the whole member's value (in a local
 * aggregate in the default mode; elsewhere it is not a constant);
 * otherwise it is the first scalar's, pending for init_members. Returns
 * 1 if it was the member's value. */
static int struct_value_init(int t)
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
    static_item(n, t);
    if (!keep_nodes)
        nnodes = mark;
    keep_nodes = 0;
    return 1;
}

/* An object of type t's initialiser, written as data items; returns the
 * bytes written (for an array of unknown size, what the initialiser gave). */
static int init_any(int t)
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
            return tok_wide ? init_wstring(t) : init_string(t);
        }
        if (is_struct(t) && tok != TK_STR && (pending_init >= 0 || tok != TK_P + '{') && struct_value_init(t))
            return type_size(t);
        if (pending_init >= 0 || !accept(TK_P + '{'))
            return init_members(t, 0);          /* braces elided */
        if (tok == TK_STR && (is_char_array(t) || is_wchar_array(t))) {
            if (tok_wide != is_wchar_array(t))
                error(tok_wide ? "a wide string for a char array" : "a narrow string for a wchar_t array");
            n = tok_wide ? init_wstring(t) : init_string(t);
        } else {
            n = init_members(t, 1);
        }
        accept(TK_P + ',');
        if (tok != TK_P + '}')
            skip_to_brace();
        expect(TK_P + '}', "'}'");
        return n;
    }
    mark = nnodes;
    if (pending_init >= 0) {
        n = pending_init;
        pending_init = -1;
        static_item(n, t);
    } else if (accept(TK_P + '{')) {
        if (tok == TK_STR && is_integer(t))
            error("a string cannot initialise one char");
        static_item(assign_expr(), t);
        accept(TK_P + ',');
        if (tok != TK_P + '}')
            skip_to_brace();
        expect(TK_P + '}', "'}'");
    } else {
        if (tok == TK_STR && is_integer(t))
            error("a string cannot initialise one char");
        static_item(assign_expr(), t);
    }
    if (!keep_nodes)
        nnodes = mark;
    keep_nodes = 0;
    return type_size(t);
}

/* The initialiser of an object of type *t, whose unknown array size it
 * completes. Items are written at once; a string object needed on the way
 * becomes an SO item (in_static_init). */
static void static_init(int *t)
{
    int n;

    n = init_any(*t);
    if (types[*t].kind == TY_ARRAY && types[*t].count < 0 && type_size(types[*t].base) > 0)
        *t = array_of(types[*t].base, n / type_size(types[*t].base));
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
        wide_flush();                   /* its wide strings, after it */
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

static void store_deferred(int l, int i);

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
                    init_base = 0;
                    ndeferred = 0;
                    static_init(&t);
                    local_template = 0;
                    in_static_init = 0;
                    write_ir("E");
                    wide_flush();
                    globals[g].type = t;
                    locals[l].type = t;
                    if (type_size(t) == 0)
                        error_s("size unknown for ", name_str(name));
                    /* the copy: destination, source, COPY n */
                    emit_fmt("LA @%d", l);
                    emit_fmt_s("A %s", ir_sym(g));
                    emit_fmt("COPY %d", type_size(t));
                    emit_line("DROP");
                    for (i = 0; i < ndeferred; i++)
                        store_deferred(l, i);
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

/* Local l's deferred element i: stored at its place in the object.
 *
 * As *(et *)((unsigned char *)&l + offset) = expression. */
static void store_deferred(int l, int i)
{
    int p;
    int et;

    et = deferred_type[i];
    p = node(EN_ADDR, ptr_to(locals[l].type), node(EN_LVAR, locals[l].type, -1, -1, l), -1, 0);
    p = node(EN_CAST, ptr_to(T_UCHAR), p, -1, 0);
    if (deferred_off[i] != 0)
        p = node(EN_BIN, ptr_to(T_UCHAR), p, num(deferred_off[i], T_INT), B_ADD);
    p = node(EN_DEREF, et, node(EN_CAST, ptr_to(et), p, -1, 0), -1, 0);
    emit_rvalue(node(EN_ASSIGN, et, p, deferred_node[i], 0));
    emit_line("DROP");
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
        if (tok != TK_P + ';') {
            n = expression();
            emit_rvalue(n);
            emit_line("DROP");
            nnodes = mark;
        }
        expect(TK_P + ';', "';' in for");
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
            if (!decls_ok)
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
        fprintf(stderr, "usage: cc1 in.i out.ir [-u unitname] [-w] [-Werror] [-v]\n");
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
