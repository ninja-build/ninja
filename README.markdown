

Ninja is yet another build system.  It takes as input a file
describing interdependencies of files (typically source code and
output executables) and orchestrates building them, *quickly*.

Ninja joins a sea of other build systems.  Its distinguishing goal is
to be fast.  It is born from my work on the Chromium browser project,
which has well over 20,000 source files and whose other build systems
(including one built from custom non-recursive Makefiles) take tens of
seconds to rebuild after changing one file, even on a fast computer.

Here are some of the design goals of Ninja:

* incredibly fast (i.e., instant) builds, even for very large projects.
* very little implicit policy; "explicit is better than implicit".
* get dependencies correct, in particular  situations that are difficult
  to get right with Makefiles (e.g. outputs have an implicit dependency on
  the command line used to generate them; using gcc's `-M` flags
  for header dependencies).
* when convenience and speed are in conflict, prefer speed.
* provide tools for getting insight into your build speed.

Some explicit *non-goals*:

* being convenient to write build files by hand.  *You should generate
  your ninja files via some other mechanism*.
* providing much policy about how builds work.
* being competitive in terms of user-friendliness with any of the many
  fine build systems that exist.
* forcing your project into any particular directory layout.
* providing a lot of build-time customization of the build.

Ninja has no built-in behavior describing how to build C source, or
build libraries, or install binaries.  You decide this policy when you
create your `.ninja` files.  Customization and configuration are out
of scope, instead you should provide customization in the system that
generates your `.ninja` files, like how autoconf provides
`./configure`.

## Getting Started
The included `bootstrap.sh` should hopefully produce a working `ninja`
binary.

Current usage is just

    ninja target

where `target` is a known output described by `build.ninja` in the
current directory.

## Creating .ninja files
A build file (default name: `build.ninja`) provides a list of *rules*
along with a list of *build* statements saying how to build files
using the rules.  The only other syntax is a way of declaring
(immutable) variables.

Conceptually, `build` statements describe the dependency graph of your
project, while `rule` statements describe how to generate the files
along a given edge of the graph.

Here's a basic `.ninja` file that demonstrates most of the syntax.

    rule cc
      command = gcc -c $in -o $out
    build foo.o: cc foo.c

### Rules
Rules begin with the `rule` keyword and a name for the rule, followed
by an indented set of `key = val` lines.  The `command` key is special,
as it defines the command to run.  (A full list of special keys is
provided in the reference.)

This declares a new rule named `cc`, along with the command to run.
Variables begin with a `$` and are described more fully later, but
here `$in` expands to the list of input files and `$out` to the output
file for this command.

### Build statements
Build statements begin with the `build` keyword, followed by a list of
output files, followed by a colon, followed by a rule name, followed
by a list of input files.  Such a declaration says that all of the
output files should be generated from the input files when the inputs
change.

### Variables
Despite the non-goal of being convenient to write by hand, to keep
build files readable (debuggable), Ninja supports declaring bindings
(variables).  A declaration like the following

    cflags = -g

Can be used in a rule like this:

    rule cc
      command = gcc $cflags -c $in -o $out

Variables might better be called "bindings", in that a given variable
cannot be changed, only shadowed.  Within a larger Ninja project,
different *scopes* allow variable values to be overridden.  XXX finish
describing me.

## Ninja file reference
A file is a series of declarations.  A declaration can be one of:

1. A rule declaration, which begins with `rule myrule`.
2. A build edge, which looks like `build output: rule input`.
3. Variable declarations, which look like `variable = value`.

Newlines are significant, but they can be escaped by putting a `\\`
before them.

Whitespace is only significant if it's at the beginning of a line.  If
a line is intended more than the previous one, it's considered part of
its parent's scope; if it is indented less than the previous one, it
closes the previous scope.

### Rule Declarations
XXX List special keys here.

### Special variables
`builddir` is a directory for intermediate build output.  (The name
comes from autoconf.)  It is special in that it gets a shorter alias:
`@`.  You must still be explicit in your rules.  In the following
example, the files prefixed with `@` will end up in the `out/`
subdirectory.

    builddir = out
    build @intermediate_file: combine @generated_file source_file

### Evaluation and scoping

XXX talk about where variables live, nested scopes etc
