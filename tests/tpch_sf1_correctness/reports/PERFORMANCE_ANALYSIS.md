# Where the time goes: TPC-H SF1, RapidsSE against DuckDB

First breakdown, 2026-11-22. Local machine (2 cores, 8 GB), RapidsSE built `-O1 -g`, one
node, two partitions. The same code built `-O2` on a GitHub runner (4 cores) takes 76.5 s for
the 22 queries without client start; DuckDB 2.0.1 on that machine takes 0.67 s.

`scripts/phase_times.py` runs each query with the job tag `/phaseTimes`, which makes the engine
write the time of fixed points of the job (`Step::vPhaseMark`), and the time of every
transformation of the run-time optimiser (`CardIter::vXformTimesTrace`). The per-query table is
`PHASE_TIMES.md`.

## The 22 queries together

| Phase | ms | Share of wall time |
|---|---:|---:|
| Client (JDBC client start, connection, result transfer) | 21 438 | 14 % |
| SQL compiler | 3 440 | 2 % |
| Run-time optimiser | 35 531 | 24 % |
| C compile of the generated code (gcc `-O3`) | 33 289 | 22 % |
| Execution | 53 945 | 36 % |
| Rest (job set-up between phases) | 996 | 1 % |
| **Wall** | **148 640** | |

About half of the time (optimiser + C compile, 69 s) is spent before the first row is read,
and does not depend on the size of the data.

## 1. Run-time optimiser: 24 %, mostly the check after every transformation

Of the 35.5 s, the transformations themselves take 3.5 s. **20.0 s are the validation of all
boards that `CardIter::vEndTransform` runs after every transformation** (`Bench::vValidate`),
and the rest is work outside the transformations (dissect, explicit validations).

| Query | Transformations | Validation after each one |
|---|---:|---:|
| Q2 | 272 ms | 2 131 ms |
| Q8 | 1 024 ms | 1 995 ms |

The largest transformation is the join ordering (`XF_FLATTEN_JOIN_ORDERING`): 864 ms in Q8,
137 ms in Q2.

## 2. C compile: 22 %, gcc `-O3` on every query

Every query generates C code and compiles it with `gcc -O3`; nothing is reused, not even for
the same statement. Q2 generates 12 552 lines (30 285 after preprocessing) and 343 warnings.

| gcc level on the code of Q2 | Time |
|---|---:|
| `-O3` (current) | 3 166 ms |
| `-O2` | 2 044 ms |
| `-O1` | 1 555 ms |
| `-O0` | 849 ms |

The engine also has a build-in path for tcc (`CompileLib`, `CB_TCC`), which is not built here.

## 3. Execution: 36 %, dominated by plan shape in a few queries

| Query | Execution | What the plan does |
|---|---:|---|
| Q20 | 10.6 s | Aggregates **all** of lineitem in 1994 by (partkey, suppkey): 909 455 rows into 543 210 groups (3.5 s alone), merged at partition, node and origin, then joins the groups to partsupp on **suppkey only** (`IndexJoin partsupp IxPS_SUPPKEY`, about 80 rows per group) and tests partkey afterwards. The selective side (`p_name like 'forest%'`: 8 508 partsupp rows, 54 ms) comes last. |
| Q9 | 8.1 s | not analysed yet |
| Q19 | 6.7 s | not analysed yet |
| Q7 | 5.2 s | The filter on the two nations is applied at the end (stage S11), after all customers, orders and lineitems of all 25 nations were joined. Derived from `(n1 = 'FRANCE' and n2 = 'GERMANY') or (...)`, `n1.n_name in ('FRANCE', 'GERMANY')` can be applied to the first nation scan. |
| Q18 | 4.9 s | not analysed yet |
| Q1 | 3.8 s | not analysed yet |

## The five queries furthest from DuckDB

| Query | Ratio on CI | Where the time goes (local) |
|---|---:|---|
| Q2 | 789 | optimiser 3.5 s (validation 2.1 s), C compile 3.2 s, execution 0.5 s |
| Q20 | 508 | execution 10.6 s (plan, see above), optimiser 2.3 s, C compile 1.6 s |
| Q11 | 425 | optimiser 1.6 s, C compile 1.9 s, execution 0.2 s |
| Q7 | 258 | execution 5.2 s (late nation filter), optimiser 3.0 s, C compile 2.1 s |
| Q8 | 255 | optimiser 4.1 s (join ordering 0.9 s, validation 2.0 s), C compile 2.1 s, execution 1.0 s |

For Q2, Q11 and Q8 the execution is under 1 s: their distance to DuckDB is almost all fixed
cost (optimiser and C compile). For Q20 and Q7 it is the plan.

## After the first two changes

1. **Validation at the end of each phase.** `CardIter::vValidateBench` clears the marks of the
   cards after every transformation as before, but checks every card only at the end of each
   phase and at the end of the optimiser. The job tag `/optimiser_validate` restores the check
   after every transformation; the equivalence test runs every query with it.
2. **Cache of compiled query code.** `CompileLib` keeps the library compiled from a source in
   `queryLibs/cache/<build>/`, keyed by a 128-bit hash of the source and of the compile command.
   The same query (same text, same constants) is not compiled again. A library is copied, not
   linked, for each job (the code keeps state in static variables). The directory belongs to one
   build of the engine; the directories of other builds are removed when the node starts. Job tag
   `/noCompileCache` bypasses it.

Same machine and build as above, 22 queries, cache emptied first:

| Phase | Before | First run | Repeated run |
|---|---:|---:|---:|
| Run-time optimiser | 35 531 | 9 048 | 9 239 |
| C compile | 33 289 | 32 264 | 14 |
| Execution | 53 945 | 55 421 | 53 881 |
| **Wall** | **148 640** | **121 342** | **86 952** |

The five queries furthest from DuckDB (wall ms; optimiser; C compile):

| Query | Before | First run | Repeated run |
|---|---|---|---|
| Q2 | 8 421; 3 544; 3 210 | 6 035; 815; 3 481 | 2 454; 788; 1 |
| Q20 | 15 528; 2 275; 1 582 | 14 323; 436; 1 584 | 13 011; 484; 1 |
| Q11 | 4 859; 1 572; 1 859 | 3 461; 393; 1 584 | 1 740; 378; 1 |
| Q7 | 11 398; 2 958; 2 121 | 8 527; 605; 1 863 | 6 454; 566; 1 |
| Q8 | 8 606; 4 116; 2 116 | 5 621; 1 221; 2 146 | 3 489; 1 404; 1 |

What is left: the C compile of a query that has not run before (27 % of the first run), the
execution, above all the plans of Q20 and Q7, and the client (about 0.9 s per query, most of it
the start of the JDBC client of the test, not the engine).

## Not yet measured

* The same breakdown with the `-O2` build. The CI job records it on every run.
* Execution time per stage and per operator (Q9, Q19, Q18, Q1).
* How much of the client column is the JDBC client start (about 0.6 to 1 s per query).
