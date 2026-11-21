# TPC-H SF1 correctness: 18/22 PASS

| | |
|---|---|
| Commit | `9d2492feca4bfffddd94e5a7e5a8a130eef6eac4` |
| Run | 2026-11-21 20:24:54 JST, 1 round(s) |
| Engine | RapidsSE `bin/rapidsse`, 1 node, 2 partitions, 2 threads |
| Data | TPC-H scale factor 1, dbgen 2.17.0 (dbgen/ of this repository), checksums pinned in `reference/dataset.json` |
| Reference | DuckDB 2.0.1 |
| Numeric tolerance | 0.01 on decimal columns; everything else exact |
| Client overhead | about 978 ms of every time below is the JDBC client starting |

| Query | Parser | Binder | Logical plan | Optimizer | Physical plan | Runtime | Result | Time ms | Failure class | Final status | Fixed by |
|---|---|---|---|---|---|---|---|---:|---|---|---|
| Q01 | PASS | PASS | PASS | PASS | PASS | PASS | FAIL | 6253 | WRONG_RESULT | **WRONG_RESULT** |  |
| Q02 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 7585 |  | **PASS** | PR #9 |
| Q03 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3791 |  | **PASS** |  |
| Q04 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2093 |  | **PASS** |  |
| Q05 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5893 |  | **PASS** |  |
| Q06 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2038 |  | **PASS** |  |
| Q07 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 10572 |  | **PASS** |  |
| Q08 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7872 |  | **PASS** |  |
| Q09 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 13452 |  | **PASS** |  |
| Q10 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5294 |  | **PASS** |  |
| Q11 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 4114 |  | **PASS** |  |
| Q12 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2961 |  | **PASS** |  |
| Q13 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3282 |  | **PASS** |  |
| Q14 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 2808 |  | **PASS** |  |
| Q15 | PASS | PASS | PASS | FAIL | NOT_RUN | NOT_RUN | NOT_RUN |  | OPTIMIZER_ERROR | **OPTIMIZER_ERROR** |  |
| Q16 | PASS | PASS | PASS | FAIL | NOT_RUN | NOT_RUN | NOT_RUN |  | OPTIMIZER_ERROR | **OPTIMIZER_ERROR** |  |
| Q17 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 7139 |  | **PASS** |  |
| Q18 | PASS | PASS | PASS | PASS | PASS | PASS | FAIL | 8194 | WRONG_RESULT | **WRONG_RESULT** |  |
| Q19 | PASS | PASS | PASS | NOT_RUN | NOT_RUN | PASS | PASS | 10047 |  | **PASS** |  |
| Q20 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 15160 |  | **PASS** | PR #11, #12, #13, #15 |
| Q21 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 5855 |  | **PASS** | PR #16 |
| Q22 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 3122 |  | **PASS** | PR #16 |

## Remaining blockers

- **Q01** WRONG_RESULT: row 3 is not in the reference: ['N', 'O', '74476040.00', '111701729697.74', '106118230307.6056', '110367043872.65417']
- **Q15** OPTIMIZER_ERROR: Error: EC_NOT_IMPLEMENTED; Feature not yet implemented
- **Q16** OPTIMIZER_ERROR: Error: EC_NONE; none
- **Q18** WRONG_RESULT: rows 1 and 2 are not in ORDER BY order

## Known defects seen in this run

- count(*) of the replicated table region returns 10, not 5
- count(*) of the replicated table nation returns 50, not 25

Stage columns: Parser, Binder and Logical plan come from `EXPLAIN` (SQL compiler); Optimizer and Physical plan from the optimised pipeline of the run-time optimiser; Runtime from the execution; Result from the comparison with the reference. Plans, logs and rows of every query are next to this file.
