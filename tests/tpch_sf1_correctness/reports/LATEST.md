# TPC-H SF1 correctness: 20/22 PASS

| | |
|---|---|
| Commit | `46c05bf9e505bd0d268ed53bd991e63798c30720 + uncommitted changes in src/` |
| Run | 2026-11-21 21:37:38 JST, 1 round(s) |
| Engine | RapidsSE `bin/rapidsse`, 1 node, 2 partitions, 2 threads |
| Data | TPC-H scale factor 1, dbgen 2.17.0 (dbgen/ of this repository), checksums pinned in `reference/dataset.json` |
| Reference | DuckDB 2.0.1 |
| Numeric tolerance | 0.01 on decimal columns; everything else exact |
| Client overhead | about 933 ms of every time below is the JDBC client starting |

| Query | Parser | Binder | Logical plan | Optimizer | Physical plan | Runtime | Result | Time ms | Failure class | Final status | Fixed by |
|---|---|---|---|---|---|---|---|---:|---|---|---|
| Q01 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 6657 |  | **PASS** |  |
| Q02 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7585 |  | **PASS** | PR #9 |
| Q03 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 3325 |  | **PASS** |  |
| Q04 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2022 |  | **PASS** |  |
| Q05 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5539 |  | **PASS** |  |
| Q06 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 1918 |  | **PASS** |  |
| Q07 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 10287 |  | **PASS** |  |
| Q08 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7610 |  | **PASS** |  |
| Q09 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 13785 |  | **PASS** |  |
| Q10 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 5426 |  | **PASS** |  |
| Q11 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 4086 |  | **PASS** |  |
| Q12 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3150 |  | **PASS** |  |
| Q13 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3117 |  | **PASS** |  |
| Q14 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2783 |  | **PASS** |  |
| Q15 | PASS | PASS | PASS | FAIL | NOT_RUN | NOT_RUN | NOT_RUN |  | OPTIMIZER_ERROR | **OPTIMIZER_ERROR** |  |
| Q16 | PASS | PASS | PASS | FAIL | NOT_RUN | NOT_RUN | NOT_RUN |  | OPTIMIZER_ERROR | **OPTIMIZER_ERROR** |  |
| Q17 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 6447 |  | **PASS** |  |
| Q18 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7489 |  | **PASS** | PR #18 |
| Q19 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 9214 |  | **PASS** |  |
| Q20 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 14420 |  | **PASS** | PR #11, #12, #13, #15 |
| Q21 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5273 |  | **PASS** | PR #16 |
| Q22 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3188 |  | **PASS** | PR #16 |

## Remaining blockers

- **Q15** OPTIMIZER_ERROR: Error: EC_NOT_IMPLEMENTED; Feature not yet implemented
- **Q16** OPTIMIZER_ERROR: Error: EC_NONE; none

## Known defects seen in this run

- count(*) of the replicated table region returns 10, not 5
- count(*) of the replicated table nation returns 50, not 25

Stage columns: Parser, Binder and Logical plan come from `EXPLAIN` (SQL compiler); Optimizer and Physical plan from the optimised pipeline of the run-time optimiser; Runtime from the execution; Result from the comparison with the reference. Plans, logs and rows of every query are next to this file.
