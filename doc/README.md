This directory contains the Ninja manual and support files used in
building it.  Here's a brief overview of how it works.

The source text, `manual.asciidoc`, is written in the AsciiDoc format.
AsciiDoc can generate HTML but it doesn't look great; instead, we use
AsciiDoc to generate the Docbook XML format and then provide our own
Docbook XSL tweaks to produce HTML from that.

In theory using AsciiDoc and DocBook allows us to produce nice PDF
documentation etc.  In reality it's not clear anyone wants that, but the
build rules are in place to generate it if you install dblatex.
