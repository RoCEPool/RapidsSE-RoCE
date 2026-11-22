# TPC-H SF1 correctness: 22/22 PASS

| | |
|---|---|
| Commit | `8253add12d01790680ac7707dd274555d2935e22` |
| Run | 2026-11-22 10:03:58 JST, 1 round(s) |
| Engine | RapidsSE `bin/rapidsse`, 1 node, 2 partitions, 2 threads |
| Data | TPC-H scale factor 1, dbgen 2.17.0 (dbgen/ of this repository), checksums pinned in `reference/dataset.json` |
| Reference | DuckDB 2.0.1 |
| Numeric tolerance | 0.01 on decimal columns; everything else exact |
| Client overhead | about 1111 ms of every time below is the JDBC client starting |

| Query | Parser | Binder | Logical plan | Optimizer | Physical plan | Runtime | Result | Time ms | Failure class | Final status | Fixed by |
|---|---|---|---|---|---|---|---|---:|---|---|---|
| Q01 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7253 |  | **PASS** | PR #19 |
| Q02 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 8216 |  | **PASS** | PR #9 |
| Q03 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3948 |  | **PASS** |  |
| Q04 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2412 |  | **PASS** |  |
| Q05 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 6427 |  | **PASS** |  |
| Q06 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2189 |  | **PASS** |  |
| Q07 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 10845 |  | **PASS** |  |
| Q08 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 8075 |  | **PASS** |  |
| Q09 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 13790 |  | **PASS** |  |
| Q10 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5786 |  | **PASS** |  |
| Q11 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 4884 |  | **PASS** |  |
| Q12 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3379 |  | **PASS** |  |
| Q13 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3273 |  | **PASS** |  |
| Q14 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3177 |  | **PASS** |  |
| Q15 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 4925 |  | **PASS** | PR #21 |
| Q16 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 4989 |  | **PASS** | PR #20 |
| Q17 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 6923 |  | **PASS** |  |
| Q18 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 9088 |  | **PASS** | PR #18 |
| Q19 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 10060 |  | **PASS** |  |
| Q20 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 15139 |  | **PASS** | PR #11, #12, #13, #15 |
| Q21 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5570 |  | **PASS** | PR #16 |
| Q22 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3575 |  | **PASS** | PR #16 |

## Remaining blockers

None.

## Known defects seen in this run

- count(*) of the replicated table region returns 10, not 5
- count(*) of the replicated table nation returns 50, not 25

Stage columns: Parser, Binder and Logical plan come from `EXPLAIN` (SQL compiler); Optimizer and Physical plan from the optimised pipeline of the run-time optimiser; Runtime from the execution; Result from the comparison with the reference. Plans, logs and rows of every query are next to this file.
