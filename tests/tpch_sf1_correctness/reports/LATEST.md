# TPC-H SF1 correctness: 22/22 PASS

| | |
|---|---|
| Commit | `11ee52a542ed34dd028a238dede419277939197b` |
| Run | 2026-11-22 00:33:29 JST, 3 round(s) |
| Engine | RapidsSE `bin/rapidsse`, 1 node, 2 partitions, 2 threads |
| Data | TPC-H scale factor 1, dbgen 2.17.0 (dbgen/ of this repository), checksums pinned in `reference/dataset.json` |
| Reference | DuckDB 2.0.1 |
| Numeric tolerance | 0.01 on decimal columns; everything else exact |
| Client overhead | about 994 ms of every time below is the JDBC client starting |

| Query | Parser | Binder | Logical plan | Optimizer | Physical plan | Runtime | Result | Time ms | Failure class | Final status | Fixed by |
|---|---|---|---|---|---|---|---|---:|---|---|---|
| Q01 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 6974 |  | **PASS** | PR #19 |
| Q02 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7626 |  | **PASS** | PR #9 |
| Q03 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3504 |  | **PASS** |  |
| Q04 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2266 |  | **PASS** |  |
| Q05 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5869 |  | **PASS** |  |
| Q06 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2100 |  | **PASS** |  |
| Q07 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 11233 |  | **PASS** |  |
| Q08 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7838 |  | **PASS** |  |
| Q09 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 14559 |  | **PASS** |  |
| Q10 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5679 |  | **PASS** |  |
| Q11 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 4697 |  | **PASS** |  |
| Q12 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3382 |  | **PASS** |  |
| Q13 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3021 |  | **PASS** |  |
| Q14 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2980 |  | **PASS** |  |
| Q15 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 4568 |  | **PASS** | PR #21 |
| Q16 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 6055 |  | **PASS** | PR #20 |
| Q17 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7133 |  | **PASS** |  |
| Q18 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 8388 |  | **PASS** | PR #18 |
| Q19 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 10718 |  | **PASS** |  |
| Q20 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 16563 |  | **PASS** | PR #11, #12, #13, #15 |
| Q21 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5496 |  | **PASS** | PR #16 |
| Q22 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3783 |  | **PASS** | PR #16 |

## Remaining blockers

None.

## Known defects seen in this run

- count(*) of the replicated table region returns 10, not 5
- count(*) of the replicated table nation returns 50, not 25

## Rounds

| Round | PASS | Total time ms |
|---|---|---:|
| 1 | 22/22 | 138270 |
| 2 | 22/22 | 141898 |
| 3 | 22/22 | 144432 |

Stage columns: Parser, Binder and Logical plan come from `EXPLAIN` (SQL compiler); Optimizer and Physical plan from the optimised pipeline of the run-time optimiser; Runtime from the execution; Result from the comparison with the reference. Plans, logs and rows of every query are next to this file.
