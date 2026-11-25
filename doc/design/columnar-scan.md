# Columnar scan for RapidsSE (phase 1)

Status: phase 1 (fixed-width columns of table scans) and 1b (string
columns, unpruned scans) implemented in `src/store/ColCache.cpp` and
`CombinedSrc::vColCacheInflate`.

## 1. Why

On TPC-H SF1 RapidsSE is 8.2x DuckDB overall (CI, after PR #46). Sampling the
22 queries shows where the CPU goes:

| Query | Decoding rows in the scan | Columns the scan reads |
|---|---|---|
| Q1  | 64% | 7 of 16 (lineitem) |
| Q6  | 60% | 4 of 16 |
| Q12 | 54% | 5 of 16 |
| Q14 | 42% | 4 of 16 |
| Q15 | 43% | 4 of 16 |
| Q18 | 34% | 2 of 5 (after the projection) |
| Q4  | 20% | 3 of 9 (orders) |

Join stages also decode whole rows of the joined table after an index lookup
(Q13: about 80% of the time, inflating orders rows to keep 2 of 9 columns;
Q3, Q5, Q7, Q8, Q10 and Q21: 45-67% of the time in join stages).

Every row of a CRW buffer (compact row buffer) is decoded column by column
from a meta-code stream: to reach column 10 the scan steps over columns 0-9,
for every row. DuckDB reads only the columns a query needs, stored as
fixed-width vectors.

Two simpler ideas were measured first and rejected:
- Block min/max (zone maps) on l_shipdate: TPC-H data is not ordered by date;
  each of the 1465 blocks of 4096 lineitem rows spans 1992-1998, so none can
  be skipped.
- Index range scans on IxL_SHIPDATE: a typed literal (DATE '...') bound is not
  accepted today; accepting it gave correct results but was 2-4x slower than
  the full scan (Q14 299 -> 749 ms), because each row is fetched and decoded
  on its own.

## 2. What phase 1 does

Keep the CRW buffers as the storage of record. Next to each CRW buffer keep a
column cache: for each column that a scan has asked for, an array with one
8-byte cell per row of the buffer, holding exactly the value the CRW inflater
would put in the cell (`m_iAsI8` of `Int_T`, `Bcd_T`, `Dte_T`, ...).

A scan whose columns are all in the cache reads the cells from the arrays
instead of decoding the row. Everything after the inflate (filters,
projection, aggregation, tub output) is unchanged.

### 2.1 Cache key and validity

- Key: the CRW buffer (its address), checked against the first head and the
  number of heads it was built from.
- The entry is dropped whenever its buffer is constructed (also when a
  paged-in buffer reuses the memory), destroyed, appended to, copied, has its
  nub restored after a page-in, or has a row deleted (hooks in
  `CrwBuffer.cpp`). Truncate drops the whole cache. A buffer with deleted
  heads is never served from the cache.
- Arrays handed to a running scan are pinned to its `Dynamic_T` until that
  scan asks again, so dropping an entry never frees memory under a scan.
- Any invalid or missing entry makes that buffer use the CRW path, so the
  result never depends on the cache.

### 2.2 Building

Lazily, the first time a scan asks for a column of a buffer: the buffer's
rows are inflated once with the existing inflater (`RowInflator`) and the
asked-for columns are copied into arrays. The first query pays about one
normal scan; later queries read arrays.

Memory: 8 bytes per row per cached column (lineitem SF1: 48 MB per column).
The cache has a byte budget (environment variable `SE_COLCACHE_MB`; by
default a quarter of the physical memory; the lineitem columns that the 22
TPC-H queries read take about 0.6 GB at SF1). A column that does not fit is not cached and its scans keep the CRW
path (an LRU is a later refinement).

### 2.3 Which columns

Phase 1 cached fixed-width columns only (INTEGER, DECIMAL, DATE, TIME,
TIMESTAMP, FLOAT, BOOLEAN). Phase 1b adds CHAR/VARCHAR: the entry keeps the
strings of the column (each with its terminating NUL, as the CRW encoding
stores it) and the cells hold their addresses, as the CRW inflater's cells
hold addresses into the string area of the row (`SI_STRAREA_ADR`). A NULL
string is `C_DATUM_NULL`, as from the inflater.

### 2.4 Code generation

`CombinedSrc` generates the CRW handler of a scan stage. It adds:

1. At the first row of each buffer, one call
   `iExtern_ColCache(aDynamicT, uColMask)` for the columns the scan reads;
   it points `m_aaColCache[c]` at the arrays (building the missing ones in
   one pass) or returns 0, and then the handler takes the CRW path for the
   whole buffer.
2. In the row loop, instead of the meta-code walk, one load per needed
   column: `pSR_IT_0->C<n>_<NAME>.m_iAsI8 = aDynamicT->m_aaColCache[n][i_Row];`

This is done after column pruning for a scan behind which filters and a
project read the row, and right after the inflate code for other scans
(which then read all their columns).

`vDynamicExecute` passes the CRW buffer to the handler (new field
`m_pCrwBuffer` in `Dynamic_T`).

Job tag `/optimiser_off_col_cache` turns the cache path off (for the
optimiser equivalence test and for A/B timing).

## 3. Later phases

- 1c: index joins fetch only the needed columns of the joined row from the
  cache by `Rrf` (buffer, item): Q13, Q3, Q5, Q7, Q8, Q10, Q21.
- 2: evaluate the filter on a batch of rows with a selection vector before
  loading the other columns (Q6, Q14, Q15).
- 3: hash join with a Bloom filter on the join key pushed into the scan of
  the probe side (Q9, Q17, Q19, Q20, Q8, Q2, Q16; Q5, Q7, Q21 by nation).

## 4. Verification

- SF1 22/22 against the reference results, with and without
  `/optimiser_off_col_cache`.
- Regression batch: no new failures.
- New test `Regression/colCache.test`: the same scan before and after an
  appending load, a truncate and a reload, and with the cache switched off.
- `run_optimiser_equivalence.py` gets the `col_cache` switch.
- A/B timing: server-side execute time per query, and paired CI runs.

## 5. Measured gain (phase 1, local SF1, execute time)

| Query | CRW path | Column cache |
|---|---|---|
| Q6  | 268 ms | 110 ms |
| Q14 | 285 ms | 161 ms |
| Q15 | 248 ms | 135 ms |

Phase 1b (string columns; scans whose columns are not pruned, such as a
GROUP BY right behind the scan):

| Query | CRW path | Column cache |
|---|---|---|
| Q1  | 499 ms | 334 ms |
| Q12 | 329 ms | 205 ms |
| Q18 | 667 ms | 619 ms |

The join-heavy queries need 1c and 3.
