# TPC-H SF1 correctness: 19/22 PASS

| | |
|---|---|
| Commit | `d6c6af07126d7baf04e8f4db47aa3ecc87168f92 + the source change of pull request #18` |
| Run | 2026-11-21 21:01:14 JST, 1 round(s) |
| Engine | RapidsSE `bin/rapidsse`, 1 node, 2 partitions, 2 threads |
| Data | TPC-H scale factor 1, dbgen 2.17.0 (dbgen/ of this repository), checksums pinned in `reference/dataset.json` |
| Reference | DuckDB 2.0.1 |
| Numeric tolerance | 0.01 on decimal columns; everything else exact |
| Client overhead | about 1112 ms of every time below is the JDBC client starting |

| Query | Parser | Binder | Logical plan | Optimizer | Physical plan | Runtime | Result | Time ms | Failure class | Final status | Fixed by |
|---|---|---|---|---|---|---|---|---:|---|---|---|
| Q01 | PASS | PASS | PASS | PASS | PASS | PASS | FAIL | 6804 | WRONG_RESULT | **WRONG_RESULT** |  |
| Q02 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 7473 |  | **PASS** | PR #9 |
| Q03 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3513 |  | **PASS** |  |
| Q04 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2276 |  | **PASS** |  |
| Q05 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5979 |  | **PASS** |  |
| Q06 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 1962 |  | **PASS** |  |
| Q07 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 11143 |  | **PASS** |  |
| Q08 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 8201 |  | **PASS** |  |
| Q09 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 14651 |  | **PASS** |  |
| Q10 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5767 |  | **PASS** |  |
| Q11 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 4323 |  | **PASS** |  |
| Q12 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3257 |  | **PASS** |  |
| Q13 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3410 |  | **PASS** |  |
| Q14 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2950 |  | **PASS** |  |
| Q15 | PASS | PASS | PASS | FAIL | NOT_RUN | NOT_RUN | NOT_RUN |  | OPTIMIZER_ERROR | **OPTIMIZER_ERROR** |  |
| Q16 | PASS | PASS | PASS | FAIL | NOT_RUN | NOT_RUN | NOT_RUN |  | OPTIMIZER_ERROR | **OPTIMIZER_ERROR** |  |
| Q17 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 6797 |  | **PASS** |  |
| Q18 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7987 |  | **PASS** |  |
| Q19 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 9627 |  | **PASS** |  |
| Q20 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 15179 |  | **PASS** | PR #11, #12, #13, #15 |
| Q21 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5592 |  | **PASS** | PR #16 |
| Q22 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3218 |  | **PASS** | PR #16 |

## Remaining blockers

- **Q01** WRONG_RESULT: row 3 is not in the reference: ['N', 'O', '74476040.00', '111701729697.74', '106118230307.6056', '110367043872.65417']
- **Q15** OPTIMIZER_ERROR: Error: EC_NOT_IMPLEMENTED; Feature not yet implemented
- **Q16** OPTIMIZER_ERROR: Error: EC_NONE; none

## Known defects seen in this run

- count(*) of the replicated table region returns 10, not 5
- count(*) of the replicated table nation returns 50, not 25

Stage columns: Parser, Binder and Logical plan come from `EXPLAIN` (SQL compiler); Optimizer and Physical plan from the optimised pipeline of the run-time optimiser; Runtime from the execution; Result from the comparison with the reference. Plans, logs and rows of every query are next to this file.
