# Join hash: the rows of an index by key, kept between queries

## What was measured first

The plan was to hand the rows that a join matches on in batches (fetch their
columns together, write them together). Measuring one join stage showed that
this is not where its time goes. Joining the 228,000 orders of one year with
lineitem at SF1 (910,000 joined rows; handler time of the stage summed over
two partitions):

| what the query does with the joined rows | stage |
|---|---|
| `sum(l_extendedprice)` (1 column) | 96 ms |
| `sum(l_extendedprice * (1 - l_discount))` (2 columns, arithmetic) | 91 ms |
| four aggregates over five columns | 95 ms |
| writes them hashed by `l_partkey` to the next stage | 82-95 ms |

One column or five, aggregated or written out: the same. The time is the
probe itself. And the stage that then joins those 910,000 rows with part took
315 ms as a hash join (it stores every arriving row, scans the keys of part,
and walks the chain of stored rows of each key: a cache miss per stored row)
and 463 ms as an index join (two descents of the index per row).

Both repeat, for every query, work that depends on the table only.

## The join hash

`IndexTree.cpp`, `JoinHash`: for one integer-key index of a table partition,
an open-addressing table key -> (offset, count) into an array of the row
references of the index, in the order of the index. It is made by a walk of
the index, by the first join that uses the index, and kept.

A stage asks for it once (`vExtern_JoinHashAttach`, generated behind the
lookup of the index in the join's initialisation). When attached,
`vExtern_IndexTreePositionGE_int` hands out the range of a key from it with
one probe instead of descending the index, and the generated code steps
through the range 8 bytes at a time. Everything else in the join is as
before, and the rows of a key come in the order the index gives them. So it
serves every join that positions an integer-key index: inner, outer, semi
and anti joins, the first operator of a stage or not.

When it is used:

- Only when the rows of a key are few (at most two a key on average) or
  stored together (lineitem by `l_orderkey`: the rows of a key follow each
  other in a buffer). For lineitem by `l_partkey` (30 rows a key in 30
  buffers) reading the rows of a key is 30 places in the table; the scanning
  hash join reads the table in storage order and stays.
- A join hash of at most 16 MB (SF1: part, partsupp, customer, supplier,
  nation) stays in the cache: a hash join on such an index stores no rows,
  each arriving row is joined at once.
- With a larger one (orders, lineitem) a probe is a cache miss or two, and
  the scan of the stored keys wins once the join has more than a few rows per
  hundred of the table. A hash join stores its rows and chooses as before; the
  joins that descended the index (the replay of a hash join with few rows,
  outer, semi and anti joins) take their ranges from the join hash.

Validity: the catalogue epoch of the column cache, the insert and delete
counts of the tree and its root buffer. A stale join hash is rebuilt by the
next join; a running stage keeps the one it attached (shared pointer). All of
them together stay below 2 GB; beyond that an index is joined as before.

Job tag `/optimiser_off_join_hash`: as before.

## Found on the way (not changed here)

With or without the join hash, `NOT EXISTS` drops the rows of the outer table
whose key is NULL, and a left outer join returned only its matched rows in a
small test (6 rows, one NULL key, one key without a match). To be looked at
separately.

## Test

`Regression/joinHash.test`: inner, grouped, semi and anti joins, again after
rows are loaded into both tables, after a truncate and a reload, and after
the table is dropped and made again; each with and without the tag.
