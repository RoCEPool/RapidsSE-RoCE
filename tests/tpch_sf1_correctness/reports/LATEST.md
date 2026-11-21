# TPC-H SF1 correctness: 21/22 PASS

| | |
|---|---|
| Commit | `edb7a6ffe2d95af055d12dfddac3fb0d47c6e772 + the source change of pull request #20` |
| Run | 2026-11-21 22:26:25 JST, 1 round(s) |
| Engine | RapidsSE `bin/rapidsse`, 1 node, 2 partitions, 2 threads |
| Data | TPC-H scale factor 1, dbgen 2.17.0 (dbgen/ of this repository), checksums pinned in `reference/dataset.json` |
| Reference | DuckDB 2.0.1 |
| Numeric tolerance | 0.01 on decimal columns; everything else exact |
| Client overhead | about 963 ms of every time below is the JDBC client starting |

| Query | Parser | Binder | Logical plan | Optimizer | Physical plan | Runtime | Result | Time ms | Failure class | Final status | Fixed by |
|---|---|---|---|---|---|---|---|---:|---|---|---|
| Q01 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7113 |  | **PASS** | PR #19 |
| Q02 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 8089 |  | **PASS** | PR #9 |
| Q03 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 3459 |  | **PASS** |  |
| Q04 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2276 |  | **PASS** |  |
| Q05 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5739 |  | **PASS** |  |
| Q06 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2167 |  | **PASS** |  |
| Q07 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 10997 |  | **PASS** |  |
| Q08 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 8083 |  | **PASS** |  |
| Q09 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 14581 |  | **PASS** |  |
| Q10 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 5713 |  | **PASS** |  |
| Q11 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 4455 |  | **PASS** |  |
| Q12 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3222 |  | **PASS** |  |
| Q13 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3072 |  | **PASS** |  |
| Q14 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2803 |  | **PASS** |  |
| Q15 | PASS | PASS | PASS | FAIL | NOT_RUN | NOT_RUN | NOT_RUN |  | OPTIMIZER_ERROR | **OPTIMIZER_ERROR** |  |
| Q16 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5732 |  | **PASS** | PR #20 |
| Q17 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7096 |  | **PASS** |  |
| Q18 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 8182 |  | **PASS** | PR #18 |
| Q19 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 9654 |  | **PASS** |  |
| Q20 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 14726 |  | **PASS** | PR #11, #12, #13, #15 |
| Q21 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5533 |  | **PASS** | PR #16 |
| Q22 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3421 |  | **PASS** | PR #16 |

## Remaining blockers

- **Q15** OPTIMIZER_ERROR: Error: EC_NOT_IMPLEMENTED; Feature not yet implemented

## Known defects seen in this run

- count(*) of the replicated table region returns 10, not 5
- count(*) of the replicated table nation returns 50, not 25

Stage columns: Parser, Binder and Logical plan come from `EXPLAIN` (SQL compiler); Optimizer and Physical plan from the optimised pipeline of the run-time optimiser; Runtime from the execution; Result from the comparison with the reference. Plans, logs and rows of every query are next to this file.
