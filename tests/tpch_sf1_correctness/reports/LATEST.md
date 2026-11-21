# TPC-H SF1 correctness: 22/22 PASS

| | |
|---|---|
| Commit | `ff26a29d467fd6a44749c4dadf924cd08ac3e3f2` |
| Run | 2026-11-22 00:09:27 JST, 3 round(s) |
| Engine | RapidsSE `bin/rapidsse`, 1 node, 2 partitions, 2 threads |
| Data | TPC-H scale factor 1, dbgen 2.17.0 (dbgen/ of this repository), checksums pinned in `reference/dataset.json` |
| Reference | DuckDB 2.0.1 |
| Numeric tolerance | 0.01 on decimal columns; everything else exact |
| Client overhead | about 990 ms of every time below is the JDBC client starting |

| Query | Parser | Binder | Logical plan | Optimizer | Physical plan | Runtime | Result | Time ms | Failure class | Final status | Fixed by |
|---|---|---|---|---|---|---|---|---:|---|---|---|
| Q01 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 6342 |  | **PASS** | PR #19 |
| Q02 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7183 |  | **PASS** | PR #9 |
| Q03 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3401 |  | **PASS** |  |
| Q04 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2165 |  | **PASS** |  |
| Q05 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5594 |  | **PASS** |  |
| Q06 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 1834 |  | **PASS** |  |
| Q07 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 10120 |  | **PASS** |  |
| Q08 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 7450 |  | **PASS** |  |
| Q09 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 13271 |  | **PASS** |  |
| Q10 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5599 |  | **PASS** |  |
| Q11 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3997 |  | **PASS** |  |
| Q12 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2832 |  | **PASS** |  |
| Q13 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2996 |  | **PASS** |  |
| Q14 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2925 |  | **PASS** |  |
| Q15 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 4404 |  | **PASS** | PR #21 |
| Q16 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 4981 |  | **PASS** | PR #20 |
| Q17 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 6740 |  | **PASS** |  |
| Q18 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7955 |  | **PASS** | PR #18 |
| Q19 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 9234 |  | **PASS** |  |
| Q20 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 14487 |  | **PASS** | PR #11, #12, #13, #15 |
| Q21 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5064 |  | **PASS** | PR #16 |
| Q22 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3248 |  | **PASS** | PR #16 |

## Remaining blockers

None.

## Known defects seen in this run

- count(*) of the replicated table region returns 10, not 5
- count(*) of the replicated table nation returns 50, not 25

## Rounds

| Round | PASS | Total time ms |
|---|---|---:|
| 1 | 22/22 | 144389 |
| 2 | 22/22 | 141424 |
| 3 | 22/22 | 131822 |

Stage columns: Parser, Binder and Logical plan come from `EXPLAIN` (SQL compiler); Optimizer and Physical plan from the optimised pipeline of the run-time optimiser; Runtime from the execution; Result from the comparison with the reference. Plans, logs and rows of every query are next to this file.
