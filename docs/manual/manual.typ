// manual.typ - the agonc manual's print layout, for Typst. build.py writes
// the chapters as body.typ beside a copy of this file and compiles it.
//
// For printing at a shop or at home: US Letter, double-sided, bound at the
// left (comb or coil). Page 1, the cover, is the only page in colour, with
// a white border so that no printer has to print to the edge; page 2 is
// blank, so the contents open on a right-hand page. Inside everything is
// black, white and grey: code and tables are told apart by shading and
// rules, never by colour. The inner margin is the wider, mirrored left
// and right; each chapter opens on a right-hand page; the page count is
// even. Only Typst's own fonts are used, so the PDF is the same anywhere.

#let version = sys.inputs.at("version", default: "dev")

#set document(title: "agonc: a C compiler for the Agon", author: "Adam Hemphill")
#set text(font: "Libertinus Serif", size: 10.5pt, lang: "en", region: "gb")
#set par(justify: true, leading: 0.62em, spacing: 1.0em)
#show raw: set text(font: "DejaVu Sans Mono", size: 0.82em)   // in proportion: headings and tables too

// ---- page 1: the cover ----
#page(paper: "us-letter", margin: 0.5in, numbering: none)[
  #block(width: 100%, height: 100%, fill: rgb("#0f3b5f"), inset: 0.65in)[
    #set text(fill: white)
    #set par(justify: false)
    #v(1.1in)
    #text(size: 60pt, weight: "bold")[agonc]
    #v(0.05in)
    #text(size: 22pt)[A C compiler for the Agon]
    #v(0.3in)
    #line(length: 45%, stroke: 2.5pt + rgb("#f2a541"))
    #v(0.3in)
    #text(size: 14pt)[User manual]
    #linebreak()
    #text(size: 12pt)[Version #version]
    #v(1fr)
    // a field of character cells, as on the Agon's screen
    #grid(columns: 16, gutter: 4pt,
      ..range(80).map(i => box(width: 19pt, height: 19pt,
        fill: if calc.rem(i * 7 + 3, 11) == 0 { rgb("#f2a541") }
              else if calc.rem(i * 5, 7) == 0 { rgb("#3d7ea6") }
              else { rgb("#1b4f78") })))
    #v(0.45in)
    #text(size: 13pt)[Adam Hemphill]
  ]
]

// ---- page 2: blank ----
#page(paper: "us-letter", numbering: none)[]

// ---- the rest ----

// A page left empty so that a chapter can start on the right: no header
// or page number on it.
#let is-blank(p) = {
  let opens = query(heading.where(level: 1)).map(h => h.location().page())
  let ends = query(<chapter-end>).map(e => e.location().page())
  (opens.contains(p + 1) and ends.contains(p - 1)) or ends.contains(p - 1) and p == counter(page).final().first() + 2
}

#set page(
  paper: "us-letter",
  margin: (inside: 1.0in, outside: 0.75in, top: 0.9in, bottom: 0.85in),
  header: context {
    let p = here().page()
    if is-blank(p) { return }
    let hs = query(heading.where(level: 1))
    if hs.any(h => h.location().page() == p) { return }
    let before = hs.filter(h => h.location().page() < p)
    if before.len() == 0 { return }
    set text(size: 8.5pt, style: "italic")
    let t = before.last().body
    if calc.odd(counter(page).get().first()) { h(1fr); t } else { t; h(1fr) }
    v(-4pt)
    line(length: 100%, stroke: 0.4pt + luma(140))
  },
  footer: context {
    if is-blank(here().page()) { return }
    let n = counter(page).get().first()
    set text(size: 9pt)
    if calc.odd(n) { h(1fr); str(n) } else { str(n); h(1fr) }
  },
)
#counter(page).update(1)

#set heading(numbering: (..n) => if n.pos().len() == 1 { str(n.pos().first()) } else { none })
#show heading.where(level: 1): it => {
  [#metadata("end") <chapter-end>]
  pagebreak(to: "odd", weak: true)
  v(0.7in)
  if it.numbering != none {
    text(size: 13pt, fill: luma(90))[Chapter #counter(heading).display()]
    v(0.05in)
  }
  text(size: 26pt, weight: "bold", it.body)
  v(0.35in)
}
#show heading.where(level: 2): it => block(above: 1.6em, below: 0.8em, sticky: true,
  text(size: 14.5pt, weight: "bold", it.body))
#show heading.where(level: 3): it => block(above: 1.3em, below: 0.6em, sticky: true,
  text(size: 11.5pt, weight: "bold", it.body))

// code and tables in greyscale
#show raw.where(block: true): it => block(width: 100%, fill: luma(242), inset: 7pt, radius: 2pt,
  stroke: 0.5pt + luma(185), breakable: true, it)
#show raw.where(block: false): it => box(fill: luma(238), inset: (x: 2pt), outset: (y: 2.2pt), radius: 1pt, it)
#set table(inset: (x: 5pt, y: 4pt),
  stroke: (x, y) => (top: if y == 0 { 0.8pt } else if y == 1 { 0.6pt } else { 0.25pt + luma(170) },
                     bottom: 0.8pt))
#show table.cell.where(y: 0): set text(weight: "bold")
#show table: set par(justify: false)
#show table: set text(size: 9.5pt)
#show figure.where(kind: table): set block(breakable: true)
#show figure.where(kind: table): set align(left)
#show link: it => if type(it.dest) == str { underline(stroke: 0.4pt + luma(120), offset: 2pt, it) } else { it }

// ---- the contents ----
#{
  show heading: it => { v(0.7in); text(size: 26pt, weight: "bold", it.body); v(0.35in) }
  outline(title: [Contents], depth: 2, indent: 1.5em)
}

#include "body.typ"

// an even number of pages: a last, blank, left-hand page if need be
#[#metadata("end") <chapter-end>]
#context {
  let last = query(<chapter-end>).last()
  if calc.odd(counter(page).at(last.location()).first()) { pagebreak() }
}
