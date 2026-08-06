# Scheduler trace and replay

Scheduler tracing is an opt-in diagnostic facility. Ordinary Ninja builds do
not construct a recorder, allocate per scheduling event, serialize data, or
perform trace file I/O.

## Commands

Record a build:

```text
ninja --scheduler-trace build.trace [targets...]
```

Validate the result against the currently loaded manifest without running any
commands:

```text
ninja --scheduler-replay build.trace
```

Reproduce a failure at the stable edge identity shown in a previous trace:

```text
ninja --scheduler-trace injected.trace \
  --scheduler-failure e0123456789abcdef=17 [targets...]
```

The requested status must be nonzero. The command is not launched. Its
injected status and causal position are recorded, and normal failure handling
continues. It is an error if the requested edge is not reached.

Reduce a failed trace to the selected edge's prerequisites, dynamic graph
changes, outcomes, and overlapping finite-pool contenders:

```text
ninja -t trace-reduce INPUT EDGE_ID OUTPUT
```

The reduced trace retains the original graph compatibility digest and can be
passed directly to `--scheduler-replay`.

The safety limits default to 1,000,000 events, 256 MiB per artifact, and 1 MiB
per line. `--scheduler-trace-max-events` and
`--scheduler-trace-max-bytes` can lower or raise the artifact limits.

## Stream format

The format is UTF-8-compatible, percent-escaped, tab-delimited text. It is
intended to be both machine-readable and convenient to inspect in tests.
Every record starts with a record kind; remaining columns are `name=value`.
Tabs, newlines, carriage returns, percent signs, and control bytes in values
are encoded as `%hh`.

```text
ninja_scheduler_trace  version=2  requires=causal-v2  graph=...
event  seq=1  type=plan  edge=e...  lane=1  label=obj/a.o  ...
event  seq=2  type=eligible  edge=e...  lane=2  previous=1  ...
end  status=success  events=2
```

Actual separators are tabs. Scheduler events have contiguous, monotonically
increasing `seq` values. Edge events also have contiguous per-edge `lane`
ordinals. Output is written and flushed one record at a time; the recorder
retains only the latest lane ordinal per graph edge. No global trace-I/O lock
is used because scheduling hooks run on Ninja's scheduler thread.

The terminal `end` record is required. A killed writer leaves a parseable
prefix without that record, and replay reports it as truncated. A recorder
limit or I/O failure makes the requested traced build fail and, when possible,
writes a `trace_error` terminal status. Replay never treats that status as a
complete build.

## Identities and graph compatibility

An edge identity is `e` followed by a 64-bit stable hash of the canonical first
declared output path. Ninja already requires that output to be unique. Dyndep
implicit outputs are appended and therefore cannot change the identity. Every
edge event also carries the unhashed first output as `label` for diagnostics.
Pointers, allocation order, and unordered-container order are never serialized.
Hash lengths are encoded as fixed-width, little-endian 64-bit values, so
identities do not vary between 32-bit and 64-bit hosts.

The header's 128-bit graph digest is calculated immediately after manifest
loading, before dependency logs or dyndep files mutate the in-memory graph. It
covers sorted edge definitions (outputs, inputs, validations, rule, expanded
command, input/output kinds, dependency-discovery bindings, pool, dyndep
binding, restat and generator settings) and pool depths. A different digest is
a compatibility error before event replay begins.

## Causal replay semantics

Version 2 is a partial-order trace. `lane` is a stable event ordinal local to
one edge, and `previous` names the preceding ordinal in that lane. `cause`
and `cause_event` name another edge and its lane ordinal for readiness,
dynamic graph updates, and pool releases. The file's `seq` is only the
observation order used for streaming; it does not impose a dependency between
otherwise independent command completions. Swapping independent completions
therefore does not renumber causal references throughout the trace, and both
orders validate.

Replay checks, at the first relevant mismatch:

- plan and current graph definitions;
- prerequisite success before eligibility;
- finite-pool reservations, delays, releases, and selections;
- recorded command-runner capacity before starts;
- selection, start, outcome, completion, and injected-status lifecycles;
- dyndep inputs, outputs, and restat changes at their discovery point;
- depfile/MSVC discoveries (recorded with `scope=next_build` because Ninja
  stores them for subsequent builds);
- restat suppression of work that was initially planned.

An event that schedules an edge before a later recorded dynamic update for
that edge is rejected even if the final graph would otherwise make it legal.
Tracing starts before target scanning. Persisted dep-log or already-present
dyndep information loaded during that scan is emitted as a stable graph delta
with `visibility=initial_scan`, before plan and readiness events.
Manifest regeneration is not itself injected or traced as a user command; the
header records the successful regeneration generation whose final manifest
formed the traced graph.

## Compatibility policy

Readers accept versions 1 and 2. Version 1 uses `requires=global-v1` and its
legacy globally observed event order. Version 2 uses `requires=causal-v2`.
Writers emit only version 2. Tests cover direct replay of version 1 artifacts;
reduction rewrites a retained version 1 slice as version 2.

The following fields are required for correct interpretation:

- header: `version`, `requires`, and `graph`;
- event: `seq` and `type`; edge lifecycle events also require `edge` and
  `lane`;
- plan: `label`, `rule`, `pool`, and `pool_depth`;
- plan dependency and dynamic input: `dependency` when the input has a
  producing edge;
- dynamic input/output: `path`; dynamic restat: `enabled=1`;
- delay and capacity: a known `reason`, plus numeric `available` for
  command-runner and jobserver capacity;
- outcome/injection: numeric `status` and outcome `source`;
- discovered dependency: `path` and `scope=next_build`;
- restat observation: `output`, `changed`, and `mode`;
- completion: `result`;
- terminal record: `status` and `events`.

All other fields are optional annotations. Readers ignore unknown fields.
They reject an unknown non-optional event type or unknown `requires` semantic.
An unknown event may be skipped only when it explicitly carries `optional=1`.
